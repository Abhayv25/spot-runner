#include "spot_runner/sqs_queue.h"

#include <aws/core/client/ClientConfiguration.h>
#include <aws/sqs/SQSClient.h>
#include <aws/sqs/model/ChangeMessageVisibilityRequest.h>
#include <aws/sqs/model/DeleteMessageRequest.h>
#include <aws/sqs/model/ReceiveMessageRequest.h>
#include <aws/sqs/model/SendMessageRequest.h>

#include <stdexcept>

#include "spot_runner/errors.h"
#include "spot_runner/logger.h"

namespace spot_runner {

namespace {

// Long polling: wait up to this long for a message instead of returning
// immediately, which cuts empty receives (and cost) when the queue is idle.
constexpr int kWaitTimeSeconds = 10;

// SQS reports an operation on a stale receipt handle (the visibility timeout
// expired and the message was received again) with one of these errors.
bool is_stale_receipt(const Aws::SQS::SQSError& error) {
    const auto& name = error.GetExceptionName();
    return error.GetErrorType() == Aws::SQS::SQSErrors::RECEIPT_HANDLE_IS_INVALID ||
           name == "ReceiptHandleIsInvalid" || name == "InvalidParameterValue" ||
           name == "MessageNotInflight" || name == "AWS.SimpleQueueService.MessageNotInflight";
}

template <typename Outcome>
void check(const Outcome& outcome, const char* operation, const Job* job = nullptr) {
    if (outcome.IsSuccess()) {
        return;
    }
    const auto& error = outcome.GetError();
    if (job != nullptr && is_stale_receipt(error)) {
        throw LeaseLostError("lease on " + job->job_id + " was lost (" + error.GetMessage() + ")");
    }
    throw std::runtime_error(std::string("SQS ") + operation + ": " + error.GetMessage());
}

}  // namespace

SqsQueue::SqsQueue(const std::string& region, std::string queue_url,
                   std::string dead_letter_queue_url)
    : queue_url_(std::move(queue_url)), dead_letter_queue_url_(std::move(dead_letter_queue_url)) {
    Aws::Client::ClientConfiguration config;
    config.region = region;
    config.connectTimeoutMs = 3000;
    config.requestTimeoutMs = (kWaitTimeSeconds + 10) * 1000;  // must outlast long polling
    client_ = std::make_unique<Aws::SQS::SQSClient>(config);
}

SqsQueue::~SqsQueue() = default;

void SqsQueue::send(const std::string& queue_url, const Job& job) {
    Aws::SQS::Model::SendMessageRequest request;
    request.SetQueueUrl(queue_url);
    request.SetMessageBody(job_to_json(job));
    check(client_->SendMessage(request), "SendMessage");
}

void SqsQueue::push(const Job& job) {
    send(queue_url_, job);
}

std::optional<Job> SqsQueue::receive(std::chrono::seconds lease) {
    Aws::SQS::Model::ReceiveMessageRequest request;
    request.SetQueueUrl(queue_url_);
    request.SetMaxNumberOfMessages(1);
    request.SetVisibilityTimeout(static_cast<int>(lease.count()));
    request.SetWaitTimeSeconds(kWaitTimeSeconds);
    const auto outcome = client_->ReceiveMessage(request);
    check(outcome, "ReceiveMessage");

    const auto& messages = outcome.GetResult().GetMessages();
    if (messages.empty()) {
        return std::nullopt;
    }
    const auto& message = messages.front();
    try {
        Job job = job_from_json(message.GetBody());
        job.receipt = message.GetReceiptHandle();
        return job;
    } catch (const std::invalid_argument& e) {
        // A malformed message would be redelivered forever; park it instead.
        log::error(std::string("dropping malformed job message to dead-letter queue: ") + e.what());
        Aws::SQS::Model::SendMessageRequest park;
        park.SetQueueUrl(dead_letter_queue_url_);
        park.SetMessageBody(message.GetBody());
        check(client_->SendMessage(park), "SendMessage");
        Aws::SQS::Model::DeleteMessageRequest remove;
        remove.SetQueueUrl(queue_url_);
        remove.SetReceiptHandle(message.GetReceiptHandle());
        check(client_->DeleteMessage(remove), "DeleteMessage");
        return std::nullopt;
    }
}

void SqsQueue::change_visibility(const Job& job, int seconds) {
    Aws::SQS::Model::ChangeMessageVisibilityRequest request;
    request.SetQueueUrl(queue_url_);
    request.SetReceiptHandle(job.receipt);
    request.SetVisibilityTimeout(seconds);
    check(client_->ChangeMessageVisibility(request), "ChangeMessageVisibility", &job);
}

void SqsQueue::delete_message(const Job& job) {
    Aws::SQS::Model::DeleteMessageRequest request;
    request.SetQueueUrl(queue_url_);
    request.SetReceiptHandle(job.receipt);
    check(client_->DeleteMessage(request), "DeleteMessage", &job);
}

void SqsQueue::extend_lease(const Job& job, std::chrono::seconds lease) {
    change_visibility(job, static_cast<int>(lease.count()));
}

void SqsQueue::complete(const Job& job) {
    delete_message(job);
}

void SqsQueue::release(const Job& job, bool count_failure) {
    if (!count_failure) {
        change_visibility(job, 0);
        return;
    }
    // SQS messages are immutable, so a new attempt count means a new message.
    // Verify the lease, send the replacement, then delete the original: a crash
    // in between leaves a duplicate, never a lost job.
    change_visibility(job, 60);
    Job next = job;
    ++next.attempts;
    send(queue_url_, next);
    delete_message(job);
}

void SqsQueue::dead_letter(const Job& job) {
    change_visibility(job, 60);
    Job parked = job;
    ++parked.attempts;
    send(dead_letter_queue_url_, parked);
    delete_message(job);
}

}  // namespace spot_runner
