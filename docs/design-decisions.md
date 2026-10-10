# Design decisions

Each entry records what was chosen, what else was considered, and why.

## 1. Application-level checkpoints instead of hibernation or VM snapshots

**Chosen:** jobs save their own progress to a file, which the manager copies to S3.

**Alternatives:** EC2 Spot hibernation (RAM saved to the EBS root volume),
CRIU process snapshots.

**Why:** a hibernated instance can resume only when capacity for the same
instance type returns in the same Availability Zone, because its EBS volume is
zonal; AWS gives no timeline. A checkpoint in S3 can be resumed immediately on
any instance type in any zone. The cost is that jobs must implement the
checkpoint contract, which is a small amount of code for most batch workloads.
CRIU is fragile across kernel versions and does not move between instance types.

## 2. One manager per instance, pulling from a queue

**Chosen:** every worker runs a manager that pulls jobs from SQS.

**Alternative:** a central scheduler that pushes work to instances.

**Why:** with pull, there is no coordinator to keep highly available, workers
can be added or removed without registration, and a vanished instance needs no
detection: its lease simply expires. SQS provides the at-least-once delivery
and visibility timeout semantics the design relies on.

## 3. Leases (visibility timeouts) for ownership

A claimed job is invisible for `lease_duration_s`, renewed every third of
that. If the owner disappears, the job reappears by itself. The lease length is
a trade-off: shorter means faster recovery after a hard failure, longer means
more tolerance for a slow renewal. Recovery after a hard kill in the chaos test
is dominated by this value, as expected.

## 4. Renew the lease immediately before every upload (fencing)

A manager that stalled (long GC-like pause, network partition) may still think
it owns a job after its lease expired and someone else claimed it. Renewing
right before each upload makes a stale owner fail with `LeaseLostError` and
abandon the job instead of overwriting a newer checkpoint. A small window
remains between the renewal and the upload; closing it fully would need a
conditional write (S3 `If-Match` on an ETag, or a version number in the key).
That is noted as future work.

## 5. Interruptions do not count as failures

The attempt counter lives in the message body and is incremented only for
crashes. Using SQS's `ApproximateReceiveCount` would count every interruption,
so a long job on a busy Spot market could be dead-lettered without ever
failing. The SQS redrive policy is kept as a high-threshold backstop for poison
messages that crash the manager itself.

## 6. Raw HTTP to Docker instead of an SDK or the CLI

The manager talks to the Docker Engine API with a 400-line HTTP/1.1 client
over the Unix socket. There is no official C++ Docker SDK, shelling out to the
`docker` CLI makes error handling string parsing, and libcurl would be a large
dependency for two local endpoints. The same client talks to IMDS over TCP. It
supports exactly what those services need: `Connection: close`, Content-Length
and chunked bodies, and timeouts via non-blocking sockets and `poll()`.

## 7. Interfaces with three implementations each

Every external dependency sits behind an interface with a production
implementation, a local implementation, and (for the runtime and watcher) a
fake. This is what makes the system testable on a laptop without AWS and lets
the chaos test exercise the real control loop.

## 8. `sigwait()` signal thread

Asynchronous signal handlers can only safely set a flag. A dedicated thread
that receives signals synchronously can log and call into the runner normally.
The first signal triggers the same graceful path as a Spot interruption; a
second forces an immediate exit.

## 9. Job image built `FROM scratch`

The reference job is statically linked and shipped alone in an empty image
(about 3 MB). It runs as a non-root user, has no shell or package manager to
patch, and uses the exec-form `ENTRYPOINT` so the binary is PID 1 and receives
`SIGTERM` directly. With the shell form, `/bin/sh` would swallow the signal and
every graceful stop would end in `SIGKILL` after the grace period.

## 10. Containers run as the manager's uid:gid

The checkpoint directory is a bind mount. Files written by a container running
as some other user may not be readable or removable by the manager. Passing
`User: <uid>:<gid>` on create keeps ownership consistent.

## 11. Rebalance recommendations drain instead of migrating

EC2 sends a rebalance recommendation before an interruption is certain. The
manager finishes its current job and stops taking new ones, rather than
checkpointing immediately. Migrating on every recommendation would churn jobs
that might never have been interrupted; the two-minute notice is still handled
if it arrives.

## 12. What is deliberately out of scope

- Exactly-once execution. Results are written to a key derived from the job ID,
  so a duplicate execution overwrites the same result with the same value.
- Job priorities and scheduling fairness. SQS standard queues are roughly FIFO.
- Large checkpoints. Uploads use a single PutObject; multipart upload would be
  needed above a few gigabytes.
