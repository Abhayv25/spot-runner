// =============================================================================
// fakes/fake_watcher.h: pretend 2-minute warning for tests (PHASE 6)
// =============================================================================
// STEP 1: #pragma once, include "spot_runner/interruption_watcher.h", <atomic>
//
// STEP 2: class FakeWatcher : public spot_runner::InterruptionWatcher
//   public:
//     std::atomic<bool> fire{false};   // test sets this to true to "send the warning"
//     int fire_after_checks = -1;      // optional: fire automatically after N checks
//     check(): count calls; if fire is true (or the count reached fire_after_checks),
//              return InterruptionNotice{"terminate", "test"}; else nullopt.
// =============================================================================
