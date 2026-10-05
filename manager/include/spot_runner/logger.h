// =============================================================================
// logger.h: simple timestamped logging (PHASE 3)
// =============================================================================
// Every line the manager prints goes through here, so all logs look the same:
//   2026-10-04T18:02:11Z INFO  [laptop-1] job-001 started (container 3f2a91)
// Good logs are how you'll debug this AND how you'll show it working in your demo.
//
// STEP 1: #pragma once, includes <string>, namespace spot_runner { }
//
// STEP 2: enum class LogLevel { Debug, Info, Warn, Error };
//
// STEP 3: Prototypes
//   void set_log_level(LogLevel minimum);         // lines below this level are skipped
//   void set_log_prefix(const std::string& prefix); // e.g. the manager_id
//   void log_debug(const std::string& message);
//   void log_info(const std::string& message);
//   void log_warn(const std::string& message);
//   void log_error(const std::string& message);
//
// (Simple free functions are fine here. A Logger class is optional.)
// =============================================================================
