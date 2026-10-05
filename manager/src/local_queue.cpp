// =============================================================================
// local_queue.cpp: fake SQS using folders (PHASE 4)
// =============================================================================
// STEP 1: Includes
//   "spot_runner/local_queue.h", <filesystem>, <fstream>, <sstream>, <chrono>,
//   <nlohmann/json.hpp>
//   using namespace std; namespace fs = std::filesystem; using json = nlohmann::json;
//   namespace spot_runner { ... }
//
// STEP 2: job_to_json / job_from_json (declared in job_queue.h)
//   to_json: {"job_id", "image", "env" (an object), "attempts"}. Do NOT save receipt.
//   from_json: parse with json::parse; use .at() for job_id and image (required),
//   .value("env", json::object()) and .value("attempts", 0) for optional ones.
//   Wrap parse errors in runtime_error with a helpful message.
//
// STEP 3: Small private helpers (unnamed namespace):
//   string read_file(const string& path)
//   void write_file_atomically(const string& path, const string& text)  (tmp + rename)
//   long long now_seconds()   (seconds since 1970: chrono::system_clock)
//
// STEP 4: Constructor: set pending_dir_ = queue_dir + "/pending", etc.
//   create_directories for all three.
//
// STEP 5: push(job)
//   write_file_atomically(pending_dir_ + "/" + job.job_id + ".json", job_to_json(job))
//
// STEP 6: receive(lease_seconds)
//   6a. return_expired_leases() first.
//   6b. List pending/ with fs::directory_iterator. Skip anything not ending in ".json".
//       Pick the oldest file (smallest last_write_time) so jobs run in order.
//   6c. CLAIM IT: fs::rename(pending/x.json, inflight/x.json) inside try/catch.
//       If rename throws, another manager won the race: try the next file.
//   6d. Read it, job_from_json, then add the lease: write the same JSON back with an
//       extra field "lease_until": now_seconds() + lease_seconds.
//   6e. job.receipt = the inflight path. Return the job.
//   6f. Nothing left: return nullopt.
//
// STEP 7: extend_lease(job, lease_seconds)
//   Rewrite job.receipt's file with a new lease_until. If the file is GONE, the lease
//   already expired and someone else may have it. Throw runtime_error("lease lost").
//   JobRunner must treat "lease lost" seriously (stop the container, don't upload).
//
// STEP 8: complete(job): fs::remove(job.receipt)
//
// STEP 9: release(job, count_as_failure)
//   Copy the job; if count_as_failure, attempts += 1.
//   Write it to pending/<id>.json (atomically), THEN remove the inflight file.
//   Order matters: if you crash between the two steps, a duplicate is safer than a
//   lost job. Write that sentence in your README; it's "at-least-once delivery".
//
// STEP 10: dead_letter(job): move the inflight file into dead/.
//
// STEP 11: return_expired_leases()
//   For each file in inflight/: read lease_until; if now_seconds() > lease_until,
//   rename it back to pending/ (ignore errors; someone else may be doing the same).
// =============================================================================
