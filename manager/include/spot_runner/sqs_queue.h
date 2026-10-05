// =============================================================================
// sqs_queue.h: JobQueue backed by Amazon SQS (PHASE 7)
// =============================================================================
// Only compiled when SPOT_RUNNER_ENABLE_AWS is ON.
//
// STEP 1: #pragma once, include "spot_runner/job_queue.h", <memory>, namespace spot_runner
//   Forward-declare:  namespace Aws::SQS { class SQSClient; }
//
// STEP 2: class SqsQueue : public JobQueue
//   public:
//     SqsQueue(const std::string& region, const std::string& queue_url);
//     ~SqsQueue() override;
//     The 6 interface functions with "override".
//   private:
//     std::string queue_url_;
//     std::unique_ptr<Aws::SQS::SQSClient> client_;
// =============================================================================
