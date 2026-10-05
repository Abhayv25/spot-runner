// =============================================================================
// job_runner.h: THE BRAIN of the manager (PHASE 6)
// =============================================================================
// Takes jobs from the queue, runs them in containers, uploads checkpoints, and
// handles the 2-minute warning. It only knows the INTERFACES (Storage, JobQueue,
// ContainerRuntime, InterruptionWatcher), never the real classes. So the same
// JobRunner works on your Mac, on AWS, and in tests with fakes.
//
// STEP 1: #pragma once
//   includes: config.h, storage.h, job_queue.h, container_runtime.h,
//             interruption_watcher.h, <atomic>, <chrono>
//   namespace spot_runner
//
// STEP 2: enum class JobOutcome
//   Finished,        // exit 0, result uploaded, job deleted from queue
//   Checkpointed,    // stopped on purpose, checkpoint uploaded, job put back
//   Failed,          // crashed, put back with attempts + 1
//   GaveUp           // crashed too many times, sent to dead letters
//
// STEP 3: struct RunnerStats (you'll put these numbers in your README!)
//   int jobs_finished = 0, jobs_checkpointed = 0, jobs_failed = 0;
//   int checkpoints_uploaded = 0;
//   bool interrupted = false;
//
// STEP 4: class JobRunner
//   public:
//     JobRunner(const Config& config, Storage& storage, JobQueue& queue,
//               ContainerRuntime& runtime, InterruptionWatcher& watcher);
//         (Take references and store references. JobRunner does NOT own these.)
//
//     // Main loop. Returns when interrupted, when request_stop() is called, or
//     // (if max_jobs > 0) after that many jobs. max_jobs makes testing easy.
//     RunnerStats run(int max_jobs = 0);
//
//     // Called from the manager's own SIGTERM/SIGINT handler (via a flag in main.cpp).
//     void request_stop();
//
//   private:
//     JobOutcome run_one(Job& job);
//     void restore_checkpoint(const Job& job, const std::string& local_dir);
//     bool upload_checkpoint_if_changed(const Job& job, const std::string& local_dir);
//     JobOutcome finish(Job& job, int exit_code, const std::string& local_dir);
//     bool should_stop();      // watcher says interrupt OR request_stop() was called
//     std::string absolute_job_dir(const Job& job) const;
//
//     const Config& config_;
//     Storage& storage_;
//     JobQueue& queue_;
//     ContainerRuntime& runtime_;
//     InterruptionWatcher& watcher_;
//     std::atomic<bool> stop_requested_{false};
//     RunnerStats stats_;
//     (plus a file_time_type to remember the last uploaded checkpoint's modified time)
// =============================================================================
