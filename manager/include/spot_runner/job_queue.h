#pragma once

#include <chrono>
#include <map>
#include <optional>
#include <string>

namespace spot_runner {

struct Job {
    std::string job_id;
    std::string image;
    std::map<std::string, std::string> env;
    // Number of failed attempts so far. Interruptions do not count: being
    // reclaimed by AWS is not the job's fault.
    int attempts = 0;

    // Claim ticket for the current lease (SQS receipt handle, or the local
    // queue's lease token). Not serialized.
    std::string receipt;
};

std::string job_to_json(const Job& job);
// Throws std::invalid_argument on malformed input.
Job job_from_json(const std::string& text);

// At-least-once work queue with leases (SQS calls them visibility timeouts).
//
// receive() hides the job from other consumers for `lease`. If the consumer
// neither completes, releases, nor extends the lease in time, the job becomes
// visible again and another manager can take it. That is what makes a machine
// that disappears without warning safe: its jobs come back on their own.
//
// Operations that take a Job throw LeaseLostError if the lease has expired and
// the job may now belong to someone else.
class JobQueue {
public:
    virtual ~JobQueue() = default;

    virtual void push(const Job& job) = 0;
    virtual std::optional<Job> receive(std::chrono::seconds lease) = 0;
    virtual void extend_lease(const Job& job, std::chrono::seconds lease) = 0;

    // The job is done; remove it permanently.
    virtual void complete(const Job& job) = 0;

    // Make the job visible again immediately. If `count_failure` is true the
    // attempt counter is incremented first.
    virtual void release(const Job& job, bool count_failure) = 0;

    // Give up on the job and park it for inspection.
    virtual void dead_letter(const Job& job) = 0;
};

}  // namespace spot_runner
