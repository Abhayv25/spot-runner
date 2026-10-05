// =============================================================================
// file_watcher.h: fake interruption warning for your Mac (PHASE 6)
// =============================================================================
// If the file exists, act like Amazon just sent a 2-minute warning.
// You trigger it from a terminal with:   touch run/interrupt-laptop-1
//
// STEP 1: #pragma once, include "spot_runner/interruption_watcher.h", namespace spot_runner
//
// STEP 2: class FileWatcher : public InterruptionWatcher
//   public:
//     explicit FileWatcher(const std::string& trigger_path);
//     std::optional<InterruptionNotice> check() override;
//   private:
//     std::string trigger_path_;
// =============================================================================
