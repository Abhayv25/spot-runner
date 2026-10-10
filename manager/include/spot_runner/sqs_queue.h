#pragma once

#include <memory>
#include <string>

#include "spot_runner/job_queue.h"

namespace Aws::SQS {
class SQSClient;
}

namespace spot_runner {

// JobQueue backed by Amazon SQS (standard queue).
//
//   receive       ReceiveMessage with VisibilityTimeout = lease, long polling
//   extend_lease  ChangeMessageVisibility(receipt, lease)
//   complete      DeleteMessage(receipt)
//   release       ChangeMessageVisibility(receipt, 0) to make it visible now, or,
//                 when counting a failure, SendMessage(attempts + 1) then DeleteMessage
//   dead_letter   SendMessage to the dead-letter queue, then DeleteMessage
//
// The attempt counter lives in the message body rather than SQS's
// ApproximateReceiveCount, because interruptions must not count as failures.
class SqsQueue : public JobQueue {
public:
    SqsQueue(const std::string& region, std::string queue_url, std::string dead_letter_queue_url);
    ~SqsQueue() override;

    SqsQueue(const SqsQueue&) = delete;
    SqsQueue& operator=(const SqsQueue&) = delete;

    void push(const Job& job) override;
    std::optional<Job> receive(std::chrono::seconds lease) override;
    void extend_lease(const Job& job, std::chrono::seconds lease) override;
    void complete(const Job& job) override;
    void release(const Job& job, bool count_failure) override;
    void dead_letter(const Job& job) override;

private:
    void send(const std::string& queue_url, const Job& job);
    void change_visibility(const Job& job, int seconds);
    void delete_message(const Job& job);

    std::string queue_url_;
    std::string dead_letter_queue_url_;
    std::unique_ptr<Aws::SQS::SQSClient> client_;
};

}  // namespace spot_runner
