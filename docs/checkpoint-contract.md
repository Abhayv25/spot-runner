# The Checkpoint Contract

This is the agreement between the **manager** and any **job**. Every job that follows these
rules can be run by the manager, whatever the job actually does inside its box. Read this
before writing `jobs/prime-counter/main.cpp` and `manager/src/job_runner.cpp`, because both
must agree on it exactly.

## 1. What the manager gives the job (environment variables)

| Variable | Example | Meaning |
|---|---|---|
| `JOB_ID` | `job-001` | The job's name. Use it in log messages. |
| `CHECKPOINT_DIR` | `/checkpoint` | The shared folder. Read and write save files here and nowhere else. |
| anything in the job message's `env` | `TARGET=300000000` | Job-specific settings. The manager passes these through untouched. |

`CHECKPOINT_DIR` is always `/checkpoint` inside the container. On the outside, the manager
connects it to `<work_dir>/<job_id>/` on the computer (a Docker **bind mount**).

## 2. Files in the shared folder

| File | Who writes it | Meaning |
|---|---|---|
| `checkpoint.dat` | job | The save file. The **manager never reads what's inside**, it only uploads and downloads it. The format belongs to the job. |
| `result.dat` | job | Written once, only when the job has fully finished. |
| `*.tmp` | job | Half-written files. The manager must ignore these. |

**Atomic writes rule:** the job must never write `checkpoint.dat` directly. It writes
`checkpoint.dat.tmp`, flushes it to disk, then **renames** it to `checkpoint.dat`. A rename is
all-or-nothing, so the manager can never see a half-written save file.

## 3. How the job starts

1. If `/checkpoint/checkpoint.dat` exists, load it and continue from there.
2. Otherwise, start from the beginning.
3. Print one log line saying which of these happened (this is how you'll prove resume works).

## 4. While the job runs

- **Autosave** every `AUTOSAVE_SECONDS` (default 10) using the atomic writes rule.
- Check a "should I stop?" flag often (at least once per second of work).

## 5. How the job stops (exit codes)

| Exit code | Meaning | What the manager does |
|---|---|---|
| `0` | Finished. `result.dat` is written. | Uploads the result, deletes the job from the queue. |
| `75` | Asked to stop, final checkpoint saved. | Uploads the checkpoint, puts the job back on the queue. |
| anything else | Crashed or failed. | Counts an attempt. Retries from the last uploaded checkpoint, or gives up after `max_attempts`. |

`75` is the standard Unix code for "temporary failure, try again later" (`EX_TEMPFAIL`).

**The stop signal:** Docker sends the program `SIGTERM` when asked to stop. The job catches
it, sets its "should I stop?" flag, writes a final checkpoint, and exits with `75`. The job
should also treat `SIGINT` (Ctrl+C) the same way, so you can test it without Docker.

If the job doesn't exit within the grace period, Docker kills it with `SIGKILL`, which can't
be caught. That's why autosave matters: the last autosave is the fallback.

## 6. Where things live in storage

Storage keys (folder-like paths in the local storage folder or the S3 bucket):

```
jobs/<job_id>/checkpoint.dat
jobs/<job_id>/result.dat
```

## 7. The job message (what sits on the queue)

```json
{
  "job_id": "job-001",
  "image": "prime-counter:latest",
  "env": { "TARGET": "300000000", "AUTOSAVE_SECONDS": "5" }
}
```

The manager adds bookkeeping (how many attempts, the lease) itself. You never write that by hand.
