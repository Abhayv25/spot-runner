# spot-runner

Run long jobs on cheap, interruptible cloud computers (AWS spot instances, ~70% off) without
losing work. When Amazon sends its 2-minute warning, the manager tells the job to save,
uploads the save, and puts the job back on the queue so another computer resumes it.

C++20 · raw HTTP over Unix sockets · Docker Engine API · AWS (EC2 Spot, S3, SQS, ECR, IMDS)

> This README is your **build guide** for now. When the project works, rewrite the top as a
> showcase: what it does, a diagram, demo GIF, and measured results.

---

## START HERE: how this repo is set up

- **Config files are done:** CMake, presets, VS Code settings, formatting, CI, local manager configs.
- **Every code file contains step-by-step instructions** as comments. Replace them with your code.
- **Docker and AWS files are yours:** `Dockerfile`, `.dockerignore` and `infra/` contain guidance only.
- Read `docs/checkpoint-contract.md` and `docs/architecture.md` before writing code.

## One-time setup (Mac)

1. `xcode-select --install` (C++ compiler)
2. Install Homebrew, then `brew install cmake`
3. Install **Docker Desktop** and start it
4. Open this folder in VS Code and install the recommended extensions when it asks
5. Build:
   ```bash
   cmake --preset debug           # first time downloads JSON + GoogleTest libraries
   cmake --build --preset debug
   ctest --preset debug
   ```
   The build will **fail at the linking step** until you write `main()` in Phase 1 and
   Phase 3. That's expected.

## Folder map

```
spot-runner/
├── jobs/prime-counter/      the fake long job (Phase 1) + its Dockerfile (Phase 2)
├── manager/
│   ├── include/spot_runner/ headers: prototypes + interfaces
│   └── src/                 definitions
├── tests/                   GoogleTest tests + fakes/
├── config/                  manager settings for local runs (2 copies = 2 "computers")
├── examples/jobs/           sample job messages to put on the queue
├── docs/                    the contract + architecture
├── infra/                   AWS setup guide (yours)
└── .github/workflows/       CI: build + test on Mac and Linux
```

---

## Build order (check these off as you go)

Each phase ends with something you can run. Commit to GitHub after every phase.

### Phase 1: the job (no Docker yet)
- [ ] `jobs/prime-counter/main.cpp`
- [ ] Pass tests A-D at the bottom of that file (finish, Ctrl+C + resume, kill -9 + resume, same answer)

### Phase 2: the job in Docker (yours)
- [ ] `jobs/prime-counter/.dockerignore`
- [ ] `jobs/prime-counter/Dockerfile`
- [ ] `docker stop` gives exit code 75 and a re-run resumes from the same shared folder

### Phase 3: manager skeleton
- [ ] `logger.h/.cpp`
- [ ] `config.h/.cpp` + `tests/test_config.cpp`
- [ ] `main.cpp` steps 1-4 (parse args, load config, print it)

### Phase 4: fake storage and fake queue
- [ ] `storage.h`, `local_storage.h/.cpp` + `tests/test_local_storage.cpp`
- [ ] `job_queue.h`, `local_queue.h/.cpp` + `tests/test_local_queue.cpp`
- [ ] `main.cpp` step 6 (`--enqueue`)

### Phase 5: talking to Docker
- [ ] `http_client.h/.cpp` + `tests/test_http_parsing.cpp`
- [ ] `container_runtime.h`, `docker_client.h/.cpp`
- [ ] `main.cpp` step 5: `--check-docker` prints "Docker OK"

### Phase 6: the brain
- [ ] `interruption_watcher.h`, `file_watcher.h/.cpp`
- [ ] `tests/fakes/fake_runtime.h`, `tests/fakes/fake_watcher.h`
- [ ] `job_runner.h/.cpp` + `tests/test_job_runner.cpp`
- [ ] `main.cpp` step 7
- [ ] **The local demo below works**

### Phase 7: real AWS services (follow `infra/README.md`)
- [ ] `s3_storage.h/.cpp`, `sqs_queue.h/.cpp`, `imds_watcher.h/.cpp`
- [ ] Manager on your Mac using real S3 + SQS

### Phase 8: real spot computers + proof
- [ ] ECR, IAM role, spot instances, Fault Injection Service (see `infra/README.md`)
- [ ] Measure results, rewrite this README as a showcase

---

## The local demo (end of Phase 6)

Two managers on one Mac = two "computers" sharing one fake queue and one fake storage.

```bash
docker build -t prime-counter:latest jobs/prime-counter

M=./build/debug/manager/spot-runner
$M --config config/manager.local.json --enqueue examples/jobs/job-001.json

# Terminal 1: computer #1 takes the job
$M --config config/manager.local.json

# Terminal 3, after a few autosaves: send computer #1 its "2-minute warning"
touch run/interrupt-laptop-1
#   -> terminal 1: job stopped, checkpoint uploaded, job released, manager exits

# Terminal 2: computer #2 picks it up and RESUMES (not from the start)
$M --config config/manager.local-2.json
```

Then try the nasty cases:
- `kill -9` manager #1 mid-job (no warning at all). After the lease expires, #2 resumes from the last autosave.
- Run both managers with several jobs queued: no job runs twice at the same time.
- Make the job crash (bad `TARGET`): attempts count up, then it lands in `run/queue/dead/`.

Reset everything: `rm -rf run/`

## Style notes

- `using namespace std;` in `.cpp` files only, never in headers.
- Prototypes in headers, definitions in `.cpp` files.
- Formatting is automatic on save (`.clang-format`).
