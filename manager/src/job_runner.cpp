// =============================================================================
// job_runner.cpp: THE BRAIN (PHASE 6)
// =============================================================================
// Read docs/checkpoint-contract.md again before starting. Write tests alongside
// (tests/test_job_runner.cpp) using the fakes, BEFORE trying it with real Docker.
//
// STEP 1: Includes
//   "spot_runner/job_runner.h", "spot_runner/logger.h", <filesystem>, <thread>
//   using namespace std; namespace fs = std::filesystem; namespace spot_runner { ... }
//
// STEP 2: Constructor: store all 5 references.
//
// STEP 3: request_stop(): stop_requested_ = true
//
// STEP 4: should_stop()
//   If stop_requested_ -> true.
//   If watcher_.check() has a value -> log_warn("interruption notice: <action> at <time>"),
//   set stats_.interrupted = true, set stop_requested_ = true, return true.
//   Else false.
//
// STEP 5: absolute_job_dir(job)
//   fs::absolute(config_.work_dir + "/" + job.job_id).string()
//   Docker needs an ABSOLUTE path for the shared folder. A relative one is the #1
//   reason the shared folder "doesn't work".
//
// STEP 6: run(max_jobs)
//   jobs_done = 0
//   loop:
//     6a. if should_stop() -> break
//     6b. job = queue_.receive(config_.lease_seconds)
//         nothing? sleep idle_wait_seconds (in small 200 ms steps, checking should_stop()
//         each time, so a warning isn't ignored while you sleep), then continue.
//     6c. log_info("took <job_id> (attempt <attempts + 1>)")
//     6d. outcome = run_one(job). Wrap in try/catch: if something unexpected throws,
//         log_error, then queue_.release(job, true) so the job isn't lost.
//     6e. Update stats_ from the outcome. jobs_done += 1.
//     6f. if max_jobs > 0 and jobs_done >= max_jobs -> break
//   return stats_
//
// STEP 7: run_one(job). Follow this order carefully.
//   7a. dir = absolute_job_dir(job). Delete it if it exists (stale), then create it.
//   7b. restore_checkpoint(job, dir)
//   7c. Build a ContainerSpec:
//         image = job.image, name = "spot-" + job.job_id
//         env = job.env, plus JOB_ID=job.job_id and CHECKPOINT_DIR=/checkpoint
//         host_checkpoint_dir = dir
//   7d. runtime_.ensure_image(job.image); id = runtime_.create_and_start(spec)
//   7e. THE WATCH LOOP. Every poll_interval_ms:
//         - status = runtime_.inspect(id). If !status.running -> break out with
//           status.exit_code.
//         - If should_stop():
//               log_warn("stopping <job_id> to save progress")
//               runtime_.stop(id, config_.stop_grace_seconds)   // job saves + exits 75
//               status = runtime_.inspect(id); break with its exit code.
//         - Every checkpoint_upload_interval_seconds (track with steady_clock):
//               upload_checkpoint_if_changed(job, dir)
//               queue_.extend_lease(job, config_.lease_seconds)
//               If extend_lease throws ("lease lost"): log_error, stop + remove the
//               container, return Failed WITHOUT uploading (someone else may own the
//               job now; uploading could overwrite their newer save).
//   7f. outcome = finish(job, exit_code, dir)
//   7g. runtime_.remove(id). Delete the local dir. Return outcome.
//
// STEP 8: restore_checkpoint(job, dir)
//   if storage_.get_file(checkpoint_key(job.job_id), dir + "/checkpoint.dat"):
//       log_info("restored checkpoint for <job_id>")
//   else log_info("no checkpoint, fresh start")
//   Also reset the remembered "last uploaded modified time" for this job.
//
// STEP 9: upload_checkpoint_if_changed(job, dir)
//   path = dir + "/checkpoint.dat". If it doesn't exist, return false.
//   t = fs::last_write_time(path). If t == last uploaded time, return false (nothing new).
//   storage_.put_file(checkpoint_key(job.job_id), path); remember t;
//   stats_.checkpoints_uploaded += 1; log_debug(...); return true.
//   (Never upload *.tmp files. The contract says they're half-written.)
//
// STEP 10: finish(job, exit_code, dir)
//   exit 0:
//     upload result.dat to result_key (if missing, treat as Failed: the job broke the
//     contract), upload the final checkpoint, queue_.complete(job) -> Finished
//   exit 75:
//     upload_checkpoint_if_changed, queue_.release(job, false) -> Checkpointed
//     (count_as_failure is false: being interrupted is not the job's fault!)
//   anything else (crash):
//     log the code and runtime_.logs_tail(id, 20) for debugging (pass id in, or
//     grab the logs in run_one before calling finish)
//     still upload_checkpoint_if_changed (the last autosave is still good progress)
//     if job.attempts + 1 >= config_.max_attempts -> queue_.dead_letter(job) -> GaveUp
//     else queue_.release(job, true) -> Failed
//
// THE TIMING BUDGET (put this in your README):
//   Amazon gives ~120 s. Notice detected within poll_interval (1 s) + job saves within
//   stop_grace_seconds (30 s) + upload (a few s) + release (1 s). Plenty of margin, and
//   if anything goes wrong, the last autosave (<= 5 s old) is the fallback.
// =============================================================================
