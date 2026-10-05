# Architecture

## The pieces

```
                     ┌──────────────── one cheap (spot) computer ────────────────┐
                     │                                                            │
 ┌──────────────┐    │  ┌──────────────────── manager (C++) ───────────────────┐  │
 │  Job queue   │◄───┼──┤ JobRunner                                            │  │
 │ LocalQueue / │    │  │   uses: Storage, JobQueue, ContainerRuntime,         │  │
 │ SQS          │    │  │         InterruptionWatcher (interfaces)             │  │
 └──────────────┘    │  └───────┬───────────────────┬────────────────┬────────┘  │
                     │          │ HTTP over          │ checks every    │           │
 ┌──────────────┐    │          │ Unix socket        │ second          │           │
 │   Storage    │◄───┼──────────┼─── uploads ───┐    ▼                 │           │
 │ LocalStorage │    │          ▼               │  FileWatcher /      │           │
 │ / S3         │    │   ┌─────────────┐        │  ImdsWatcher        │           │
 └──────────────┘    │   │   Docker    │        │                     │           │
                     │   └──────┬──────┘        │                     │           │
                     │          │ runs           │                     │           │
                     │   ┌──────▼──────────────┐ │                     │           │
                     │   │ job container       │ │                     │           │
                     │   │ (prime-counter)     │ │                     │           │
                     │   │   /checkpoint ◄─────┼─┴── shared folder ───┘           │
                     │   └─────────────────────┘     (bind mount)                 │
                     └────────────────────────────────────────────────────────────┘
```

## Same code, three environments

| Interface | Tests | Your Mac | AWS |
|---|---|---|---|
| Storage | LocalStorage (temp dir) | LocalStorage (`run/storage`) | S3Storage |
| JobQueue | LocalQueue (temp dir) | LocalQueue (`run/queue`) | SqsQueue |
| ContainerRuntime | FakeRuntime | DockerClient | DockerClient |
| InterruptionWatcher | FakeWatcher | FileWatcher | ImdsWatcher |

JobRunner never changes. Only `main.cpp` decides which real piece to plug in.

## Guarantees (what you'll claim, and test)

1. **No lost jobs.** A job is only deleted from the queue after its result is uploaded.
   If anything dies first, the lease runs out and the job comes back.
2. **Bounded lost work.** At most one autosave interval of work is lost per interruption,
   even if the computer vanishes instantly.
3. **No corrupt saves.** Every write is tmp-then-rename (or an S3 upload, which is
   all-or-nothing).
4. **At-least-once, not exactly-once.** In rare crash timing, a job can run twice. That's
   safe because jobs resume from checkpoints and results overwrite. Say this honestly in
   interviews; knowing the trade-off is the point.
