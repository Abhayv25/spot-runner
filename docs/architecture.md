# Architecture

## Components

```
                   +---------------------- one spot instance ----------------------+
                   |                                                                |
 +-------------+   |   +------------------------- manager -----------------------+  |
 |  job queue  |<--+---|  JobRunner (control loop)                               |  |
 |  SQS        |   |   |    depends only on four interfaces:                     |  |
 +-------------+   |   |    JobQueue, Storage, ContainerRuntime,                 |  |
                   |   |    InterruptionWatcher                                  |  |
 +-------------+   |   +------+-----------------------+-------------------------+  |
 |  storage    |<--+----------|  checkpoint uploads   | HTTP/1.1 over           |  |
 |  S3         |   |          |                       | /var/run/docker.sock    |  |
 +-------------+   |          |                       v                         |  |
                   |          |                +-------------+     IMDSv2 poll  |  |
                   |          |                |   Docker    |   (169.254.      |  |
                   |          |                +------+------+    169.254.254)  |  |
                   |          |                       | runs                    |  |
                   |          |                +------v-----------------+       |  |
                   |          +--------------->| job container          |       |  |
                   |             bind mount    | /checkpoint            |       |  |
                   |                           +------------------------+       |  |
                   +----------------------------------------------------------------+
```

| Interface | Production | Local development | Unit tests |
|---|---|---|---|
| `JobQueue` | `SqsQueue` | `LocalQueue` (directory + flock) | `LocalQueue` on a temp dir |
| `Storage` | `S3Storage` | `LocalStorage` | `LocalStorage` on a temp dir |
| `ContainerRuntime` | `DockerClient` | `DockerClient` or `ProcessRuntime` | `FakeRuntime` |
| `InterruptionWatcher` | `ImdsWatcher` | `FileWatcher` | `FakeWatcher` |

`main.cpp` is the only place that knows which implementation is in use.

## Control loop

```
run():
    remove containers left behind by a previous crash of this manager
    loop:
        if shutdown requested or interruption notice: stop
        if rebalance recommendation: stop after the current job (drain)
        job = queue.receive(lease)            # hidden from other managers
        if none: back off, retry
        run_one(job)

run_one(job):
    download checkpoint from storage into the work dir (if one exists)
    start container with the work dir mounted at /checkpoint
    every poll interval:
        container exited?                      -> finish()
        interruption or shutdown?              -> docker stop (SIGTERM, grace), finish()
        checkpoint changed since last sync?    -> renew lease, upload
        lease due for renewal?                 -> renew lease
finish(exit code):
    0         upload result, delete from queue
    75 / we stopped it   upload checkpoint, requeue (no failure counted)
    other     upload checkpoint, requeue with attempts+1 or dead-letter
```

## Failure modes

| What happens | What the system does | Work lost |
|---|---|---|
| Two-minute interruption notice | Stop job, upload final checkpoint, release to queue | None |
| Instance vanishes with no notice | Lease expires; another manager claims the job and resumes from the last uploaded checkpoint | Time since last upload (bounded by autosave + sync interval) |
| Manager process crashes | Same as above; on restart the manager removes its orphaned containers | Same |
| Job crashes | Requeue with `attempts + 1`; dead-letter after `max_attempts` | Time since last upload |
| Lease lost while running (manager stalled) | Stop the job, upload nothing, walk away | Nothing durable is overwritten |
| Rebalance recommendation | Finish the current job, take no new ones, exit | None |
| Storage or queue call fails | Exception is caught; job is requeued or its lease expires | Bounded as above |

## Guarantees

1. **No lost jobs.** A job leaves the queue only after its result is durable
   (`complete` runs after the result upload). Every other path either releases
   it or lets its lease expire.
2. **Bounded lost work.** At most one autosave interval plus one sync interval,
   even if the machine disappears instantly.
3. **No torn checkpoints.** Every write is temp-file-plus-rename or a single S3
   PutObject, both all-or-nothing.
4. **At-least-once execution, single active owner.** A job can be executed more
   than once (that is what resuming is), but leases plus renew-before-upload
   fencing keep two managers from both uploading for the same job at the same
   time. The chaos test checks this from the event logs.

## Timing budget for an interruption

EC2 gives about 120 seconds between the notice and termination.

| Step | Default (AWS config) |
|---|---|
| Notice detected | within `poll_interval` (1 s) |
| Job writes final checkpoint and exits | within `stop_grace_period_s` (60 s) |
| Upload final checkpoint, release message | typically under 1 s for small checkpoints |
| Reserve | the config loader rejects `stop_grace_period_s > 100` |

If any step overruns, the last periodic upload is the fallback.

## Concurrency model

The manager is single-threaded apart from one signal thread. `SIGINT` and
`SIGTERM` are blocked in all threads and received synchronously with
`sigwait()`, so the handler can log and call `JobRunner::request_stop()` (an
atomic store) without async-signal-safety concerns. The process runtime
unblocks the signals in the child between `fork()` and `execve()`.
