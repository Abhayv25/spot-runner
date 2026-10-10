# Checkpoint contract

Any program can run under spot-runner if it follows these rules. The manager
never looks inside a checkpoint; the format belongs entirely to the job.

## Inputs

| Variable | Set by | Meaning |
|---|---|---|
| `CHECKPOINT_DIR` | manager | Directory for checkpoints and results. `/checkpoint` in a container; a host path under the process runtime. |
| `JOB_ID` | manager | The job's ID, for log lines. |
| `SPOT_RUNNER_ATTEMPT` | manager | 1 on the first attempt, incremented after each failure. |
| anything in the job message's `env` | submitter | Job-specific settings, passed through untouched. |

## Files

| File | Written by | Meaning |
|---|---|---|
| `checkpoint.dat` | job | Latest progress. Uploaded to storage whenever it changes. |
| `result.dat` | job | Final output. Written exactly once, when the work is complete. |
| `*.tmp*` | job | In-progress writes. Ignored by the manager. |

Writes must be atomic: write a temporary file in the same directory, `fsync`
it, then `rename` it over the target. `rename` within a filesystem replaces the
destination in one step, so the manager (or a crash) can never observe half a
checkpoint.

## Lifecycle

1. **Start.** If `checkpoint.dat` exists, resume from it; otherwise start fresh.
   A checkpoint that fails to parse is treated as absent.
2. **Run.** Rewrite `checkpoint.dat` periodically. The interval bounds the work
   lost if the machine disappears without warning.
3. **Stop request.** On `SIGTERM` (and `SIGINT`), write a final checkpoint and
   exit promptly. The manager allows `stop_grace_period_s` before `SIGKILL`.
4. **Finish.** Write `result.dat`, then exit 0.

## Exit codes

| Code | Meaning | Manager action |
|---|---|---|
| 0 | Done; `result.dat` exists | Upload result, delete the job from the queue |
| 75 | Stopped on request; checkpoint saved | Upload checkpoint, requeue without counting a failure |
| other | Crashed | Upload last checkpoint, requeue with `attempts + 1`, dead-letter after `max_attempts` |

75 is `EX_TEMPFAIL` from `sysexits.h`: a temporary failure, try again later.
A job killed by a signal is reported as `128 + signal`, so `137` is `SIGKILL`.

## Storage layout

```
<prefix>jobs/<job_id>/checkpoint.dat
<prefix>jobs/<job_id>/result.dat
```

## Job message

```json
{
  "job_id": "primes-50m",
  "image": "prime-counter:latest",
  "env": { "TARGET": "50000000", "AUTOSAVE_MS": "2000" }
}
```

`job_id` may contain letters, digits, `-`, `_`, and `.` (max 128 characters),
because it becomes a file name, an S3 key segment, and a container name.
