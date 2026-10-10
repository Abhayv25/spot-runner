#include "spot_runner/job_runner.h"

#include <thread>

#include "spot_runner/errors.h"
#include "spot_runner/logger.h"

namespace spot_runner {

namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;
using nlohmann::json;

namespace {

// Exit code a job uses to say "I was asked to stop and saved my progress".
constexpr int kExitInterrupted = 75;
constexpr int kLogTailLines = 20;

long long millis_since(Clock::time_point start) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count();
}

}  // namespace

const char* to_string(JobOutcome outcome) {
    switch (outcome) {
        case JobOutcome::Completed:
            return "completed";
        case JobOutcome::Interrupted:
            return "interrupted";
        case JobOutcome::Failed:
            return "failed";
        case JobOutcome::DeadLettered:
            return "dead_lettered";
        case JobOutcome::LeaseLost:
            return "lease_lost";
    }
    return "unknown";
}

JobRunner::JobRunner(const Config& config, std::string manager_id, Storage& storage,
                     JobQueue& queue, ContainerRuntime& runtime, InterruptionWatcher& watcher,
                     EventLog& events)
    : config_(config),
      manager_id_(std::move(manager_id)),
      storage_(storage),
      queue_(queue),
      runtime_(runtime),
      watcher_(watcher),
      events_(events) {}

void JobRunner::request_stop() {
    shutdown_requested_.store(true);
}

void JobRunner::poll_signals() {
    if (shutdown_requested_.load() && stop_reason_ == StopReason::None) {
        stop_reason_ = StopReason::Shutdown;
        log::info("shutdown requested");
        events_.emit("shutdown_requested");
    }
    const auto notice = watcher_.check();
    if (!notice) {
        return;
    }
    if (notice->kind == InterruptionNotice::Kind::Interruption) {
        if (stop_reason_ != StopReason::Interruption) {
            stop_reason_ = StopReason::Interruption;
            stats_.received_interruption = true;
            log::warn("spot interruption notice: action=" + notice->action + " time=" + notice->time);
            events_.emit("interruption_notice", {{"action", notice->action}, {"time", notice->time}});
        }
    } else if (config_.drain_on_rebalance && !draining_) {
        draining_ = true;
        log::warn("rebalance recommendation: finishing current job, accepting no new work");
        events_.emit("rebalance_recommendation", {{"time", notice->time}});
    }
}

void JobRunner::sleep_interruptible(std::chrono::milliseconds duration) {
    const auto deadline = Clock::now() + duration;
    while (Clock::now() < deadline) {
        poll_signals();
        if (stop_reason_ != StopReason::None) {
            return;
        }
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now());
        std::this_thread::sleep_for(std::min(left, config_.timing.poll_interval));
    }
}

RunnerStats JobRunner::run(int max_jobs) {
    runtime_.remove_orphans(manager_id_);
    events_.emit("manager_started");
    log::info("manager started");

    int processed = 0;
    while (true) {
        poll_signals();
        if (stop_reason_ != StopReason::None || draining_) {
            break;
        }
        if (max_jobs > 0 && processed >= max_jobs) {
            break;
        }

        std::optional<Job> job;
        try {
            job = queue_.receive(config_.timing.lease_duration);
        } catch (const std::exception& e) {
            log::error(std::string("queue receive failed: ") + e.what());
            sleep_interruptible(config_.timing.idle_backoff);
            continue;
        }
        if (!job) {
            if (config_.exit_when_idle) {
                log::info("queue empty, exiting");
                break;
            }
            sleep_interruptible(config_.timing.idle_backoff);
            continue;
        }

        const JobOutcome outcome = run_one(std::move(*job));
        ++processed;
        switch (outcome) {
            case JobOutcome::Completed:
                ++stats_.completed;
                break;
            case JobOutcome::Interrupted:
                ++stats_.interrupted;
                break;
            case JobOutcome::Failed:
                ++stats_.failed;
                break;
            case JobOutcome::DeadLettered:
                ++stats_.dead_lettered;
                break;
            case JobOutcome::LeaseLost:
                ++stats_.lease_lost;
                break;
        }
    }

    events_.emit("manager_stopped", {{"completed", stats_.completed},
                                     {"interrupted", stats_.interrupted},
                                     {"failed", stats_.failed},
                                     {"dead_lettered", stats_.dead_lettered},
                                     {"lease_lost", stats_.lease_lost},
                                     {"checkpoints_uploaded", stats_.checkpoints_uploaded}});
    log::info("manager stopped");
    return stats_;
}

JobOutcome JobRunner::run_one(Job job) {
    ActiveJob active;
    active.attempt = job.attempts + 1;
    active.claimed_at = Clock::now();
    active.dir = fs::absolute(fs::path(config_.work_dir) / job.job_id);
    active.job = std::move(job);
    const std::string& id = active.job.job_id;

    try {
        // A directory left by an earlier attempt on this machine may hold a
        // stale checkpoint; durable storage is the only source of truth.
        fs::remove_all(active.dir);
        fs::create_directories(active.dir);

        const fs::path local_checkpoint = active.dir / "checkpoint.dat";
        const bool resumed = storage_.get_file(checkpoint_key(id), local_checkpoint.string());
        if (resumed) {
            // The restored file is already in storage; don't upload it back.
            active.uploaded_mtime = fs::last_write_time(local_checkpoint);
            active.uploaded_size = fs::file_size(local_checkpoint);
            active.has_uploaded = true;
        }
        log::info("claimed " + id + " (attempt " + std::to_string(active.attempt) + ", " +
                  (resumed ? "resuming from checkpoint" : "fresh start") + ")");
        events_.emit("job_claimed", {{"job", id}, {"attempt", active.attempt}, {"resumed", resumed}});

        ContainerSpec spec;
        spec.image = active.job.image;
        spec.name = "spot-runner-" + id;
        spec.env = active.job.env;
        spec.env["JOB_ID"] = id;
        spec.env["SPOT_RUNNER_ATTEMPT"] = std::to_string(active.attempt);
        spec.labels = {{"spot-runner.manager", manager_id_}, {"spot-runner.job", id}};
        spec.checkpoint_dir = active.dir.string();

        runtime_.ensure_image(spec.image);
        active.handle = runtime_.start(spec);
        events_.emit("job_started", {{"job", id}, {"attempt", active.attempt}});
    } catch (const LeaseLostError& e) {
        return handle_lease_lost(active, e.what());
    } catch (const std::exception& e) {
        return handle_failure(active, std::string("could not start job: ") + e.what());
    }

    const auto renew_every = std::chrono::duration_cast<std::chrono::milliseconds>(
                                 config_.timing.lease_duration) / 3;
    auto last_renew = Clock::now();
    auto last_sync = Clock::now();

    try {
        while (true) {
            std::this_thread::sleep_for(config_.timing.poll_interval);

            const ContainerStatus status = runtime_.inspect(active.handle);
            if (!status.exists) {
                return handle_failure(active, "job container disappeared");
            }
            if (!status.running) {
                return finish(active, status.exit_code, false);
            }

            poll_signals();
            if (stop_reason_ != StopReason::None) {
                const char* reason = stop_reason_ == StopReason::Interruption ? "interruption" : "shutdown";
                log::warn("stopping " + id + " (" + reason + "), grace period " +
                          std::to_string(config_.timing.stop_grace_period.count()) + "s");
                events_.emit("job_stopping", {{"job", id}, {"reason", reason}});
                runtime_.stop(active.handle, config_.timing.stop_grace_period);
                return finish(active, runtime_.inspect(active.handle).exit_code, true);
            }

            if (Clock::now() - last_sync >= config_.timing.checkpoint_sync_interval) {
                if (sync_checkpoint(active)) {
                    last_renew = Clock::now();  // sync renews the lease as a fencing step
                }
                last_sync = Clock::now();
            }
            if (Clock::now() - last_renew >= renew_every) {
                queue_.extend_lease(active.job, config_.timing.lease_duration);
                last_renew = Clock::now();
            }
        }
    } catch (const LeaseLostError& e) {
        return handle_lease_lost(active, e.what());
    } catch (const std::exception& e) {
        return handle_failure(active, std::string("error while supervising job: ") + e.what());
    }
}

bool JobRunner::sync_checkpoint(ActiveJob& active) {
    const fs::path path = active.dir / "checkpoint.dat";
    std::error_code ec;
    const auto mtime = fs::last_write_time(path, ec);
    if (ec) {
        return false;  // the job has not written a checkpoint yet
    }
    const auto size = fs::file_size(path, ec);
    if (ec || (active.has_uploaded && mtime == active.uploaded_mtime && size == active.uploaded_size)) {
        return false;
    }

    queue_.extend_lease(active.job, config_.timing.lease_duration);
    storage_.put_file(checkpoint_key(active.job.job_id), path.string());

    active.uploaded_mtime = mtime;
    active.uploaded_size = size;
    active.has_uploaded = true;
    ++stats_.checkpoints_uploaded;
    log::debug("uploaded checkpoint for " + active.job.job_id + " (" + std::to_string(size) + " bytes)");
    events_.emit("checkpoint_uploaded",
                 {{"job", active.job.job_id}, {"attempt", active.attempt}, {"bytes", size}});
    return true;
}

JobOutcome JobRunner::finish(ActiveJob& active, int exit_code, bool stopped_by_us) {
    const std::string& id = active.job.job_id;
    try {
        if (exit_code == 0) {
            const fs::path result = active.dir / "result.dat";
            if (!fs::exists(result)) {
                return handle_failure(active, "exited 0 without writing result.dat");
            }
            queue_.extend_lease(active.job, config_.timing.lease_duration);
            sync_checkpoint(active);
            storage_.put_file(result_key(id), result.string());
            queue_.complete(active.job);

            const long long duration = millis_since(active.claimed_at);
            log::info("completed " + id + " in " + std::to_string(duration) + " ms");
            events_.emit("job_completed",
                         {{"job", id}, {"attempt", active.attempt}, {"duration_ms", duration}});
            cleanup(active);
            return JobOutcome::Completed;
        }

        if (stopped_by_us || exit_code == kExitInterrupted) {
            sync_checkpoint(active);
            queue_.release(active.job, /*count_failure=*/false);

            const char* reason = stopped_by_us ? "interruption" : "job_exited_75";
            log::info("released " + id + " with checkpoint (" + reason + ", exit " +
                      std::to_string(exit_code) + ")");
            events_.emit("job_released", {{"job", id},
                                          {"attempt", active.attempt},
                                          {"exit_code", exit_code},
                                          {"reason", reason}});
            cleanup(active);
            return JobOutcome::Interrupted;
        }

        return handle_failure(active, "exited with code " + std::to_string(exit_code));
    } catch (const LeaseLostError& e) {
        return handle_lease_lost(active, e.what());
    }
}

JobOutcome JobRunner::handle_failure(ActiveJob& active, const std::string& reason) {
    const std::string& id = active.job.job_id;
    log::error("job " + id + " failed: " + reason);
    if (!active.handle.empty()) {
        try {
            const std::string tail = runtime_.logs_tail(active.handle, kLogTailLines);
            if (!tail.empty()) {
                log::error("last output of " + id + ":\n" + tail);
            }
        } catch (const std::exception&) {
            // Diagnostics only.
        }
    }

    JobOutcome outcome = JobOutcome::Failed;
    try {
        // Progress saved before the crash is still valid; keep it.
        if (!active.handle.empty()) {
            sync_checkpoint(active);
        }
        if (active.attempt >= config_.max_attempts) {
            queue_.dead_letter(active.job);
            outcome = JobOutcome::DeadLettered;
            log::error("job " + id + " exceeded " + std::to_string(config_.max_attempts) +
                       " attempts, moved to dead-letter queue");
            events_.emit("job_dead_lettered",
                         {{"job", id}, {"attempt", active.attempt}, {"reason", reason}});
        } else {
            queue_.release(active.job, /*count_failure=*/true);
            events_.emit("job_failed", {{"job", id}, {"attempt", active.attempt}, {"reason", reason}});
        }
    } catch (const LeaseLostError& e) {
        return handle_lease_lost(active, e.what());
    } catch (const std::exception& e) {
        // If we cannot even release the job, its lease will expire and it will
        // be retried by someone else. Nothing is lost.
        log::error("could not requeue " + id + ": " + e.what());
    }

    cleanup(active);
    return outcome;
}

JobOutcome JobRunner::handle_lease_lost(ActiveJob& active, const std::string& reason) {
    const std::string& id = active.job.job_id;
    log::error("lost lease on " + id + ": " + reason + "; abandoning without uploading");
    events_.emit("lease_lost", {{"job", id}, {"attempt", active.attempt}});
    if (!active.handle.empty()) {
        try {
            runtime_.stop(active.handle, std::chrono::seconds(5));
        } catch (const std::exception& e) {
            log::warn(std::string("stop after lease loss failed: ") + e.what());
        }
    }
    cleanup(active);
    return JobOutcome::LeaseLost;
}

void JobRunner::cleanup(ActiveJob& active) noexcept {
    if (!active.handle.empty()) {
        try {
            runtime_.remove(active.handle);
        } catch (const std::exception& e) {
            log::warn("could not remove container for " + active.job.job_id + ": " + e.what());
        }
        active.handle.clear();
    }
    std::error_code ec;
    fs::remove_all(active.dir, ec);
}

}  // namespace spot_runner
