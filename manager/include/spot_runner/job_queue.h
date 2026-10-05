// =============================================================================
// job_queue.h: the INTERFACE for "the shared to-do list" (PHASE 4)
// =============================================================================
// Real versions: LocalQueue (folders on your Mac) and SqsQueue (Amazon SQS, Phase 7).
//
// Key idea: a LEASE. When a manager takes a job, the job is hidden from everyone
// else for lease_seconds. If that manager dies and never finishes or renews, the
// lease runs out and the job reappears for someone else. SQS calls this the
// "visibility timeout". This is what makes "a computer vanished" safe.
//
// STEP 1: #pragma once, includes <string>, <map>, <optional>, namespace spot_runner
//
// STEP 2: struct Job
//   std::string job_id;
//   std::string image;
//   std::map<std::string, std::string> env;
//   int attempts = 0;          // how many times it has crashed (not interrupted!)
//   std::string receipt;       // the queue's "claim ticket" for this lease.
//                              // LocalQueue: the in-flight file path. SQS: ReceiptHandle.
//
// STEP 3: Prototypes for JSON conversion (define in local_queue.cpp)
//   std::string job_to_json(const Job& job);         // receipt is NOT saved
//   Job job_from_json(const std::string& text);      // throws on bad JSON / missing fields
//
// STEP 4: class JobQueue
//   public:
//     virtual ~JobQueue() = default;
//     virtual void push(const Job& job) = 0;                          // add a new job
//     virtual std::optional<Job> receive(int lease_seconds) = 0;      // take one, or nullopt
//     virtual void extend_lease(const Job& job, int lease_seconds) = 0; // "still working!"
//     virtual void complete(const Job& job) = 0;                      // done: delete forever
//     virtual void release(const Job& job, bool count_as_failure) = 0;// put back now
//     virtual void dead_letter(const Job& job) = 0;                   // gave up on it
// =============================================================================
