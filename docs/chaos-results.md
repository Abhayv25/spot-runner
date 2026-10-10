# Chaos test results

Recorded 2026-10-10 on a 2 vCPU Linux host with the Docker runtime and the
local queue and storage backends. Raw reports are in [chaos/](chaos/).

Settings for both scenarios: lease 6 s, checkpoint sync every 1 s, job autosave
every 1 s, stop grace period 10 s, faults split roughly evenly between graceful
interruptions and hard kills.

## How faults are injected

- **Graceful:** create the manager's interruption trigger file. This goes
  through the same code path as an EC2 Spot interruption notice from IMDS.
- **Hard:** `SIGKILL` the manager's process group and `docker kill` its
  containers, with no warning. This is what an instance that vanishes looks like.

Every killed or interrupted manager is replaced by a new one with a new ID.

## How metrics are computed

From the managers' JSON event logs plus the harness's own record of when it
injected each fault:

| Metric | Definition |
|---|---|
| Graceful handoff | `interruption_notice` to `job_released` on the same manager: the final checkpoint is in storage and the job is back in the queue |
| Graceful resume | `job_released` to the next `job_claimed` for that job on another manager |
| Hard-kill recovery | kill time to the next `job_claimed` for the job that was running |
| Work lost per hard kill | kill time minus the last `checkpoint_uploaded` for that attempt (or its start, if nothing was uploaded) |
| Concurrent ownership | any two attempts on the same job whose `[claimed, ended]` intervals overlap |
| Correct | `primes_found` in `result.dat` equals an independent sieve of Eratosthenes |

## Scenario 1: load (3 managers, 30 jobs)

Jobs count primes up to 16M to 43M. Faults every 4 to 8 s.

| | |
|---|---|
| Faults injected | 37 (14 graceful, 23 hard kills; 21 kills hit a running job) |
| Jobs correct | 30 / 30 |
| Concurrent ownership violations | 0 |
| Dead-lettered | 0 |
| Attempts started | 64 |
| Resumed from checkpoint | 33 |
| Checkpoints uploaded | 596 |

| Latency | p50 | p95 | max |
|---|---|---|---|
| Graceful handoff | 0.19 s | 0.23 s | 0.24 s |
| Graceful resume | 0.26 s | 5.88 s | 6.65 s |
| Hard-kill recovery | 6.95 s | 10.60 s | 17.39 s |
| Work lost per hard kill | 0.49 s | 0.94 s | 1.92 s |

With every manager busy, resume times include waiting for a free manager.

## Scenario 2: recovery (4 managers, 4 long jobs)

Jobs count primes up to 120M to 180M, so each one is interrupted many times.
Faults every 6 to 10 s. There is always a free manager, so resume times
measure the system rather than queueing.

| | |
|---|---|
| Faults injected | 46 (19 graceful, 27 hard kills; 24 kills hit a running job) |
| Jobs correct | 4 / 4 |
| Concurrent ownership violations | 0 |
| Attempts started | 47 |
| Resumed from checkpoint | 43 |
| Checkpoints uploaded | 1224 |

| Latency | p50 | p95 | max |
|---|---|---|---|
| Graceful handoff | 0.20 s | 0.22 s | 0.23 s |
| Graceful resume | 0.20 s | 0.26 s | 0.28 s |
| Hard-kill recovery | 5.68 s | 6.24 s | 6.28 s |
| Work lost per hard kill | 0.47 s | 0.96 s | 1.88 s |

Hard-kill recovery is bounded by the 6 s lease, as designed: the job cannot be
reclaimed until the dead manager's lease expires. Work lost is bounded by one
autosave interval plus one sync interval (2 s).

## A bug the chaos test found

The first load run had a median graceful resume of 56.8 s and a median
hard-kill recovery of 51.6 s ([report](chaos/load-before-queue-order-fix.json)).
The cause: the local queue ordered jobs by file modification time, and
releasing a job rewrote its file, so every interrupted job went to the back of
the line behind all pending work. SQS does not behave that way; resetting a
message's visibility keeps its place.

The fix stores the original enqueue time in each entry and orders by it.
Failure retries still go to the back, as a new SQS message would. After the
fix, under the same seed and load, median graceful resume fell from 56.8 s to
0.26 s and median hard-kill recovery from 51.6 s to 6.95 s. Two regression
tests cover the ordering (`InterruptedJobKeepsItsPlaceInLine`,
`FailedJobGoesToTheBackOfTheLine`).

## Reproducing

```bash
cmake --preset release && cmake --build --preset release
docker build -t prime-counter:latest jobs/prime-counter

python3 scripts/chaos_test.py --manager build/release/manager/spot-runner \
    --runtime docker --image prime-counter:latest --managers 3 --jobs 30

python3 scripts/chaos_test.py --manager build/release/manager/spot-runner \
    --runtime docker --image prime-counter:latest --managers 4 --jobs 4 \
    --min-target 120000000 --max-target 180000000 --fault-interval 6 10 --seed 11
```

Timings depend on the machine; correctness and the bounds should not.
