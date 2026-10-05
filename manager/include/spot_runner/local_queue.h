// =============================================================================
// local_queue.h: JobQueue backed by folders (PHASE 4)
// =============================================================================
// Layout under queue_dir (./run/queue):
//   pending/   job-001.json        <- waiting to be taken
//   inflight/  job-001.json        <- someone has it; file holds a "lease_until" time
//   dead/      job-001.json        <- failed too many times
//
// The trick that makes this safe with TWO managers on one Mac: rename() is atomic.
// If two managers try to move pending/job-001.json -> inflight/ at the same moment,
// exactly one succeeds and the other gets an error. That's how a job is "claimed".
//
// STEP 1: #pragma once, include "spot_runner/job_queue.h", namespace spot_runner
//
// STEP 2: class LocalQueue : public JobQueue
//   public:
//     explicit LocalQueue(const std::string& queue_dir);  // creates the 3 sub-folders
//     The 6 interface functions with "override".
//   private:
//     void return_expired_leases();      // inflight files past lease_until -> pending
//     std::string pending_dir_, inflight_dir_, dead_dir_;
// =============================================================================
