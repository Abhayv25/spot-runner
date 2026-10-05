// =============================================================================
// fakes/fake_runtime.h: pretend Docker for tests (PHASE 6)
// =============================================================================
// A header-only class (definitions inside the class body are fine for test helpers).
//
// STEP 1: #pragma once, include "spot_runner/container_runtime.h", <functional>, <vector>
//
// STEP 2: class FakeRuntime : public spot_runner::ContainerRuntime
//   Public "knobs" the test sets BEFORE running:
//     int exit_code_when_done = 0;
//     int polls_until_done = 3;          // how many inspect() calls before it "exits"
//     int exit_code_when_stopped = 75;
//     std::function<void(const std::string& shared_dir)> on_start;
//         (lets a test write checkpoint.dat / result.dat into the shared folder,
//          like a real job would)
//     std::function<void(const std::string& shared_dir, int poll)> on_poll;
//
//   Public "records" the test checks AFTERWARD:
//     std::vector<spot_runner::ContainerSpec> started_specs;
//     int stop_calls = 0, remove_calls = 0;
//     bool saw_checkpoint_at_start = false;
//
//   Behavior:
//     ping -> true; ensure_image -> do nothing
//     create_and_start -> save spec; check if spec.host_checkpoint_dir + "/checkpoint.dat"
//       exists (set saw_checkpoint_at_start); call on_start; return "fake-1"
//     inspect -> count polls; call on_poll; running until polls_until_done is reached,
//       then running=false with exit_code_when_done (or exit_code_when_stopped if
//       stop() was called)
//     stop -> stop_calls++, remember it was stopped
//     remove -> remove_calls++
//     logs_tail -> return "fake logs"
// =============================================================================
