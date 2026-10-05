// =============================================================================
// logger.cpp (PHASE 3)
// =============================================================================
// STEP 1: Includes: "spot_runner/logger.h", <iostream>, <chrono>, <ctime>, <iomanip>
//   using namespace std;  namespace spot_runner { ... }
//
// STEP 2: File-level state (put it inside an unnamed namespace { } so it's private
//   to this file; same effect as "static" globals):
//     LogLevel g_min_level = LogLevel::Info;
//     string g_prefix;
//
// STEP 3: A private helper (also in the unnamed namespace):
//   void write_line(LogLevel level, const string& message)
//     - If level < g_min_level, return. (enum class values compare in declared order)
//     - Get the current UTC time: chrono::system_clock::now() -> time_t -> gmtime_r.
//       Format with put_time(&tm, "%Y-%m-%dT%H:%M:%SZ").
//     - Level text padded to 5 chars: "DEBUG", "INFO ", "WARN ", "ERROR".
//     - Print:  time level [prefix] message
//     - Warn/Error go to cerr, the rest to cout. End with endl (flushes; logs that
//       sit in a buffer when the process is killed are lost forever).
//
// STEP 4: Define the 6 public functions; each is one line calling write_line,
//   except the two setters.
//
// Later (optional): add a LOG_FORMAT=json mode that prints one JSON object per
// line. Cloud log tools (like CloudWatch) can search those by field.
// =============================================================================
