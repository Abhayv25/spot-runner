#pragma once

#include <atomic>
#include <chrono>
#include <filesystem>
#include <string>

#include "spot_runner/config.h"
#include "spot_runner/container_runtime.h"
#include "spot_runner/event_log.h"
#include "spot_runner/interruption_watcher.h"
#include "spot_runner/job_queue.h"
#include "spot_runner/storage.h"

namespace spot_runner {

enum class JobOutcome {
    Completed,     // exit 0, result uploaded, removed from the queue
    Interrupted,   // stopped for an interruption or shutdown, checkpoint uploaded, requeued
    Failed,        // crashed, requeued with attempts + 1
    DeadLettered,  // crashed max_attempts times, parked in the dead-letter queue
    LeaseLost,     // our claim expired; another manager may own the job, so we backed off
};

const char* to_string(JobOutcome outcome);

struct RunnerStats {
    int completed = 0;
    int interrupted = 0;
    int failed = 0;
    int dead_lettered = 0;
    int lease_lost = 0;
    int checkpoints_uploaded = 0;
    bool received_interruption = false;
};

// The manager's control loop. Claims jobs from the queue, runs each one in the
// container runtime, mirrors its checkpoints to durable storage, renews its
// lease, and on an interruption notice stops the job gracefully, uploads the
// final checkpoint, and hands the job back to the queue.
//
// JobRunner depends only on interfaces, so the same code runs against AWS
// (S3, SQS, IMDS, Docker), against local stand-ins, and against test fakes.
// It does not own its collaborators; they must outlive it.
class JobRunner {
public:
    JobRunner(const Config& config, std::string manager_id, Storage& storage, JobQueue& queue,
              ContainerRuntime& runtime, InterruptionWatcher& watcher, EventLog& events);

    // Runs until interrupted, until request_stop(), until the queue is empty
    // (if exit_when_idle), or until `max_jobs` jobs have been processed (0 = no limit).
    RunnerStats run(int max_jobs = 0);

    // Graceful shutdown: the current job is checkpointed and requeued, exactly
    // as for an interruption. Safe to call from any thread.
    void request_stop();

private:
    enum class StopReason { None, Interruption, Shutdown };

    struct ActiveJob {
        Job job;
        int attempt = 0;
        std::string handle;
        std::filesystem::path dir;
        std::chrono::steady_clock::time_point claimed_at;
        std::filesystem::file_time_type uploaded_mtime{};
        std::uintmax_t uploaded_size = 0;
        bool has_uploaded = false;
    };

    JobOutcome run_one(Job job);
    JobOutcome finish(ActiveJob& active, int exit_code, bool stopped_by_us);
    JobOutcome handle_failure(ActiveJob& active, const std::string& reason);
    JobOutcome handle_lease_lost(ActiveJob& active, const std::string& reason);
    // Removes the container and the local work directory. Never throws.
    void cleanup(ActiveJob& active) noexcept;

    // Uploads the job's checkpoint if it changed since the last upload. Renews
    // the lease first, so a manager that has lost its claim cannot overwrite a
    // newer checkpoint written by the job's new owner. Throws LeaseLostError.
    bool sync_checkpoint(ActiveJob& active);

    // Checks for shutdown requests and interruption notices.
    void poll_signals();
    // Sleeps for `duration`, waking early if a stop is requested.
    void sleep_interruptible(std::chrono::milliseconds duration);

    const Config& config_;
    std::string manager_id_;
    Storage& storage_;
    JobQueue& queue_;
    ContainerRuntime& runtime_;
    InterruptionWatcher& watcher_;
    EventLog& events_;

    std::atomic<bool> shutdown_requested_{false};
    StopReason stop_reason_ = StopReason::None;
    bool draining_ = false;
    RunnerStats stats_;
};

}  // namespace spot_runner
