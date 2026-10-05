// =============================================================================
// interruption_watcher.h: the INTERFACE for "is this computer being taken away?" (PHASE 6)
// =============================================================================
// Real versions: FileWatcher (a file on your Mac you create to fake the warning)
// and ImdsWatcher (asks Amazon's metadata service, Phase 7).
//
// STEP 1: #pragma once, includes <string>, <optional>, namespace spot_runner
//
// STEP 2: struct InterruptionNotice
//   std::string action;        // "terminate", "stop" or "hibernate" (Amazon's words)
//   std::string time;          // when it happens, as text (e.g. "2026-10-04T18:05:00Z")
//
// STEP 3: class InterruptionWatcher
//   public:
//     virtual ~InterruptionWatcher() = default;
//     // nullopt = all good. A value = start shutting down NOW.
//     // Must never throw; if checking fails, log it and return nullopt.
//     virtual std::optional<InterruptionNotice> check() = 0;
// =============================================================================
