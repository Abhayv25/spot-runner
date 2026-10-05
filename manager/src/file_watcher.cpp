// =============================================================================
// file_watcher.cpp: fake 2-minute warning (PHASE 6)
// =============================================================================
// STEP 1: Includes: "spot_runner/file_watcher.h", <filesystem>
//   using namespace std; namespace spot_runner { ... }
//
// STEP 2: Constructor: store trigger_path.
//
// STEP 3: check()
//   If filesystem::exists(trigger_path_): return InterruptionNotice{"terminate", "now"}.
//   Otherwise return nullopt.
//   Use the overload of exists() that takes an error_code so it can never throw
//   (the interface promises check() never throws).
//
// Usage while the manager runs:   touch run/interrupt-laptop-1
// Reset before running it again:  rm run/interrupt-laptop-1
// =============================================================================
