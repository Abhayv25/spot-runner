// =============================================================================
// test_job_runner.cpp (PHASE 6): the most valuable tests in the project
// =============================================================================
// These run the real JobRunner with REAL LocalStorage and LocalQueue (temp folders)
// but a FAKE container runtime and a FAKE watcher, so they need no Docker and run in
// milliseconds. Include "fakes/fake_runtime.h" and "fakes/fake_watcher.h".
//
// Setup in a fixture:
//   - temp folders for storage, queue, work_dir
//   - a Config with small numbers (poll_interval_ms = 10, upload interval 1,
//     lease 5, max_attempts 3)
//   - LocalStorage, LocalQueue, FakeRuntime, FakeWatcher, then a JobRunner
//   - helper: push_job("job-001")
//
// TESTS TO WRITE:
//   1. FinishedJobIsCompleted:
//      fake runtime "finishes" with exit 0 and writes result.dat into the shared folder.
//      run(1) -> stats.jobs_finished == 1, result exists in storage, queue is empty.
//   2. InterruptionCheckpointsAndReleases:
//      fake job writes checkpoint.dat then keeps "running"; trigger the watcher.
//      -> runtime.stop was called, checkpoint in storage, job back on the queue with
//         attempts == 0, stats.interrupted == true.
//   3. ResumeRestoresCheckpoint:
//      put a checkpoint in storage first. run(1).
//      -> the fake runtime saw checkpoint.dat in the shared folder BEFORE it "started".
//   4. CrashCountsAttempt: exit code 1 -> job back on queue with attempts == 1.
//   5. GivesUpAfterMaxAttempts: job with attempts == 2, exit 1 -> in dead/ folder.
//   6. AutosavesAreUploadedWhileRunning: fake job rewrites checkpoint.dat a few
//      times while running -> stats.checkpoints_uploaded >= 2.
//   7. RequestStopActsLikeInterruption: call request_stop() from another thread.
//   8. EnvironmentIsPassedThrough: the spec the fake got has JOB_ID, CHECKPOINT_DIR=/checkpoint
//      and the job's own env values; host_checkpoint_dir is absolute.
// =============================================================================
