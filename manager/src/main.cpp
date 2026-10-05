// =============================================================================
// main.cpp: start-up and wiring (PHASE 3, then grow it in Phases 5-7)
// =============================================================================
// main() only connects pieces together. No real logic lives here, so it stays short.
//
// Usage you're building toward:
//   ./build/debug/manager/spot-runner --config config/manager.local.json
//   ./build/debug/manager/spot-runner --config config/manager.local.json --check-docker
//   ./build/debug/manager/spot-runner --config config/manager.local.json --enqueue examples/jobs/job-001.json
//
// STEP 1: Includes
//   All the spot_runner headers you need, <iostream>, <csignal>, <atomic>, <memory>,
//   <fstream>, <sstream>. Wrap the AWS headers in  #ifdef SPOT_RUNNER_WITH_AWS ... #endif
//   using namespace std; using namespace spot_runner;
//
// STEP 2: Signal handling for the MANAGER itself
//   Global: atomic<bool> g_shutdown{false};
//   handler(int) sets g_shutdown = true (nothing else, same rule as the job).
//   This lets Ctrl+C on the manager behave like an interruption: stop the job
//   gracefully, upload, put it back. Not just die.
//
// STEP 3: Prototypes (define below main)
//   void print_usage();
//   int run_manager(const Config& config);
//   int check_docker(const Config& config);
//   int enqueue_job(const Config& config, const string& job_file);
//
// STEP 4: main(int argc, char* argv[])
//   4a. Read arguments in a loop (for int i = 1; i < argc; i++):
//         "--config <path>" (required), "--check-docker", "--enqueue <file>",
//         "--verbose" (sets log level Debug), "--help".
//       Missing --config -> print_usage(), return 2.
//   4b. Config config = load_config(path) inside try/catch; on error print it and return 2.
//   4c. set_log_prefix(config.manager_id)
//   4d. Pick the mode: check_docker / enqueue_job / run_manager. Return its result.
//
// STEP 5: check_docker(config)
//   Make a DockerClient, call ping(). Print "Docker OK" or a helpful hint about the
//   socket path. Return 0 or 1. (Use this as your first Phase 5 milestone.)
//
// STEP 6: enqueue_job(config, job_file)
//   Read the file, job_from_json, make the queue (see STEP 7c), queue.push(job).
//   Print "queued <job_id>". (So you never hand-copy files into run/queue.)
//
// STEP 7: run_manager(config)
//   7a. Install the signal handler for SIGINT and SIGTERM.
//   7b. Create the real pieces as unique_ptrs to the INTERFACE types:
//         unique_ptr<Storage> storage;  unique_ptr<JobQueue> queue;
//         unique_ptr<InterruptionWatcher> watcher;
//         DockerClient docker(config.docker_socket);
//   7c. if config.mode == "local":
//         storage = make_unique<LocalStorage>(config.local.storage_dir)
//         queue   = make_unique<LocalQueue>(config.local.queue_dir)
//         watcher = make_unique<FileWatcher>(config.local.interrupt_file)
//       else (mode "aws", Phase 7, inside #ifdef SPOT_RUNNER_WITH_AWS):
//         S3Storage, SqsQueue, ImdsWatcher
//       (Without the AWS build, mode "aws" should print a clear error and return 2.)
//   7d. If !docker.ping() -> log_error and return 1.
//   7e. JobRunner runner(config, *storage, *queue, docker, *watcher);
//   7f. The signal flag must reach the runner. Simplest: start a small std::thread that
//       checks g_shutdown every 200 ms and calls runner.request_stop() when it flips.
//       (Join the thread before returning!)
//   7g. RunnerStats stats = runner.run();
//   7h. Print a summary: finished / checkpointed / failed / checkpoints uploaded /
//       interrupted yes-no. Return 0.
//
// STEP 8 (Phase 7): AWS SDK start/stop around everything AWS:
//   Aws::SDKOptions options; Aws::InitAPI(options); ... Aws::ShutdownAPI(options);
//   Make sure every S3/SQS object is destroyed BEFORE ShutdownAPI (put them in an
//   inner { } scope).
// =============================================================================
