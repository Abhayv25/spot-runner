#pragma once

#include <filesystem>
#include <string>

#include "spot_runner/job_queue.h"

namespace spot_runner {

// JobQueue backed by a directory, shared by any number of manager processes on
// one machine. It reproduces SQS lease semantics for local runs and tests:
//
//   <dir>/pending/<job>.json    visible, waiting to be claimed
//   <dir>/inflight/<job>.json   claimed; stores lease expiry and lease token
//   <dir>/dead/<job>.json       exceeded max attempts
//
// Every operation runs under an exclusive flock() on <dir>/queue.lock, so
// operations are serializable across processes and threads. Each claim
// generates a fresh random lease token; an operation presenting a stale token
// (because the lease expired and someone else re-claimed the job) fails with
// LeaseLostError, just like a stale SQS receipt handle.
class LocalQueue : public JobQueue {
public:
    explicit LocalQueue(const std::string& queue_dir);

    void push(const Job& job) override;
    std::optional<Job> receive(std::chrono::seconds lease) override;
    void extend_lease(const Job& job, std::chrono::seconds lease) override;
    void complete(const Job& job) override;
    void release(const Job& job, bool count_failure) override;
    void dead_letter(const Job& job) override;

private:
    class Lock;

    std::filesystem::path inflight_path(const std::string& job_id) const;
    // Verifies the inflight entry exists and carries `job.receipt`; throws
    // LeaseLostError otherwise. Caller must hold the lock.
    void verify_lease(const Job& job) const;
    // Moves expired inflight entries back to pending. Caller must hold the lock.
    void requeue_expired() const;

    std::filesystem::path root_;
    std::filesystem::path pending_;
    std::filesystem::path inflight_;
    std::filesystem::path dead_;
    std::filesystem::path lock_path_;
};

}  // namespace spot_runner
