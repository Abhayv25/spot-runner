# spot-runner

A C++ job runner that makes long batch jobs safe to run on EC2 Spot instances.

Spot capacity costs far less than on-demand, but AWS can reclaim an instance
with a two-minute warning, or with none at all. spot-runner puts a small
manager on every worker. The manager pulls jobs from a queue, runs each one in
a Docker container, and continuously copies the job's checkpoints to durable
storage. When an interruption notice arrives, it stops the job gracefully,
uploads the final checkpoint, and hands the job back to the queue, where
another worker resumes it from where it stopped. If a worker disappears
without warning, its claim on the job expires and another worker resumes from
the last uploaded checkpoint.

**Under fault injection** (83 random interruptions and hard kills across two
scenarios), every job finished with a result identical to an independent
reference computation and no job was ever owned by two managers at once. A
graceful handoff took 0.2 s against the 120 s budget, and a hard kill lost a
median of 0.5 s of work. Details in [Chaos testing](#chaos-testing).

C++20, POSIX sockets, Docker Engine API, Amazon S3 and SQS (AWS SDK for C++),
EC2 instance metadata (IMDSv2), Terraform, GoogleTest, GitHub Actions.

## How it works

```
  SQS queue ──receive (lease)──> manager ──HTTP over unix socket──> Docker ──> job container
      ^                            │  │                                          │
      │                            │  └── polls IMDS for the 2-minute notice      │ writes
      └──release / complete────────┘                                             v
                                   └──── uploads checkpoint.dat ────> S3   <── /checkpoint (bind mount)
```

1. **Claim.** The manager receives a job from SQS. The message becomes
   invisible to other workers for the lease duration (the visibility timeout),
   which the manager renews while the job runs.
2. **Restore.** If a checkpoint for this job exists in S3, it is downloaded
   into a work directory that is bind-mounted into the container at
   `/checkpoint`.
3. **Run.** The job runs in a container and periodically rewrites
   `checkpoint.dat` atomically. The manager uploads it whenever it changes.
4. **Interruption.** On a Spot interruption notice from the instance metadata
   service, the manager sends SIGTERM through Docker. The job saves and exits
   with code 75, the manager uploads the checkpoint and makes the message
   visible again.
5. **Finish.** On exit code 0 the manager uploads `result.dat` and deletes the
   message.

Any program can be a job if it follows the
[checkpoint contract](docs/checkpoint-contract.md): resume from
`$CHECKPOINT_DIR/checkpoint.dat` if present, save on SIGTERM and exit 75,
write `result.dat` and exit 0 when done. The repository includes a reference
job, `prime-counter`, which counts primes by trial division.

## Design highlights

- **Leases instead of a coordinator.** Ownership is an SQS visibility timeout.
  A worker that vanishes needs no failure detector; its job reappears on its
  own. ([design decision 3](docs/design-decisions.md#3-leases-visibility-timeouts-for-ownership))
- **Fencing before writes.** The manager renews its lease immediately before
  every checkpoint upload, so a stalled manager that lost its claim cannot
  overwrite progress made by the job's new owner.
- **Interruptions are not failures.** Attempts are counted in the message body,
  not with SQS's receive count, so a job interrupted many times is never
  dead-lettered for it.
- **No partial checkpoints.** Every write is temp file, fsync, rename, directory
  fsync; every upload is a single S3 PutObject.
- **Own HTTP/1.1 client.** The manager speaks the Docker Engine API over
  `/var/run/docker.sock` and IMDSv2 over TCP with a small client built on
  non-blocking sockets and `poll()`, including chunked transfer decoding.
- **Testable without AWS.** Queue, storage, container runtime, and interruption
  source are interfaces with production, local, and fake implementations.
  The same `JobRunner` runs in all three settings.
- **Synchronous signal handling.** SIGINT and SIGTERM are received by a
  dedicated `sigwait()` thread, so shutdown logic is not limited to
  async-signal-safe calls. The first signal checkpoints and requeues the
  current job; a second exits immediately.
- **3 MB job image.** `prime-counter` is statically linked and shipped
  `FROM scratch` as a non-root user, with an exec-form entrypoint so SIGTERM
  reaches the process.

The full list, with the alternatives that were rejected, is in
[docs/design-decisions.md](docs/design-decisions.md).

## Repository layout

```
manager/                 the manager (library + spot-runner executable)
  include/spot_runner/   public headers, one per component
  src/                   implementations
jobs/prime-counter/      reference job and its Dockerfile
tests/                   GoogleTest unit, integration, and end-to-end tests
scripts/chaos_test.py    fault-injection harness
infra/terraform/         S3, SQS, ECR, IAM, Spot Auto Scaling group, FIS experiment
config/                  example manager configurations
docs/                    architecture, checkpoint contract, design decisions, results
Dockerfile               manager image with the S3/SQS backends
```

| Component | File |
|---|---|
| Control loop | `manager/src/job_runner.cpp` |
| HTTP/1.1 client | `manager/src/http_client.cpp` |
| Docker Engine API client | `manager/src/docker_client.cpp` |
| Child-process runtime | `manager/src/process_runtime.cpp` |
| SQS queue | `manager/src/sqs_queue.cpp` |
| S3 storage | `manager/src/s3_storage.cpp` |
| IMDSv2 watcher | `manager/src/imds_watcher.cpp` |
| Local queue (flock + leases) | `manager/src/local_queue.cpp` |

## Building

Requirements: a C++20 compiler (GCC 12+ or Clang 15+), CMake 3.20+. GoogleTest
and nlohmann/json are downloaded automatically.

```bash
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
```

Other presets: `release`, `asan` (AddressSanitizer and UBSan), `ci` (warnings
as errors), and `aws`, which adds the S3 and SQS backends and needs the AWS SDK
for C++ installed (`-DCMAKE_PREFIX_PATH=/path/to/aws-sdk`).

## Running locally

The `local` backend replaces SQS with a directory-based queue that has the
same lease semantics, S3 with a directory, and the metadata service with a
trigger file.

```bash
docker build -t prime-counter:latest jobs/prime-counter
M=./build/debug/manager/spot-runner

$M enqueue --config config/local-docker.json examples/jobs/primes-200m.json
$M run --config config/local-docker.json
```

In another terminal, simulate the two-minute notice:

```bash
touch run/interrupt-local-1
```

The manager stops the container, uploads the checkpoint, releases the job, and
exits. Remove the trigger file and start the manager again: the job resumes
from its checkpoint (`"resumed":true` in `run/events/local-1.jsonl`).

Without Docker, use `config/local-process.json`, which runs jobs as child
processes; set the job's `image` to the path of the `prime-counter` binary.

## Testing

**Unit and integration tests** (`ctest`, 85 tests) cover configuration
validation, HTTP parsing and chunked decoding, the Docker client against a
scripted fake Docker daemon on a Unix socket, the IMDSv2 token flow against a
fake metadata server, queue lease semantics including stale receipts and
eight concurrent consumers, every `JobRunner` outcome path with a fake
runtime, and an end-to-end test that interrupts the real `prime-counter`
mid-run on one manager and resumes it on another with an identical result.

### Chaos testing

`scripts/chaos_test.py` starts several managers against a shared local queue
and storage directory, then repeatedly picks a random manager and either
creates its interruption trigger (graceful) or SIGKILLs the manager and its
container with no warning (hard). Killed managers are replaced, as an Auto
Scaling group would. At the end it verifies every result against a sieve of
Eratosthenes and computes metrics from the managers' event logs.

```bash
cmake --preset release && cmake --build --preset release
python3 scripts/chaos_test.py --manager build/release/manager/spot-runner \
    --runtime docker --image prime-counter:latest --managers 3 --jobs 30
```

Measured results (October 2026, Docker runtime, 2 vCPU Linux host):

| | Load: 3 managers, 30 jobs | Recovery: 4 managers, 4 long jobs |
|---|---|---|
| Faults injected (graceful / hard) | 37 (14 / 23) | 46 (19 / 27) |
| Jobs correct | 30 / 30 | 4 / 4 |
| Concurrent ownership violations | 0 | 0 |
| Graceful handoff, p50 / max | 0.19 s / 0.24 s | 0.20 s / 0.23 s |
| Resume after graceful handoff, p50 | 0.26 s | 0.20 s |
| Recovery after hard kill, p50 / max | 6.95 s / 17.39 s | 5.68 s / 6.28 s |
| Work lost per hard kill, p50 / max | 0.49 s / 1.92 s | 0.47 s / 1.88 s |

Hard-kill recovery is bounded by the 6 s lease used in the test, and work lost
by the 1 s autosave plus 1 s sync interval. In the load scenario, resume times
also include waiting for a free manager.

The chaos test also found a real bug: the local queue sent interrupted jobs to
the back of the line, unlike SQS. Fixing it cut median resume time after a
graceful interruption from 56.8 s to 0.26 s.

Raw reports are in [docs/chaos-results.md](docs/chaos-results.md).

## Deploying to AWS

`infra/terraform` provisions the bucket, queues, ECR repositories,
least-privilege IAM role, and a 100% Spot Auto Scaling group spread across six
instance types with Capacity Rebalancing, plus an AWS Fault Injection Service
template that sends a real Spot interruption to a worker. See
[infra/README.md](infra/README.md).

## Limitations and future work

- The fencing window between lease renewal and upload could be closed with S3
  conditional writes (`If-Match` on the previous ETag).
- Checkpoints are uploaded with a single PutObject; very large checkpoints
  would need multipart upload and possibly incremental checkpoints.
- The manager runs one job at a time per instance.
- The AWS deployment has been validated by compilation and `terraform
  validate`; the chaos results above come from the local backends.

## License

MIT
