#pragma once

#include <string>
#include <string_view>

namespace spot_runner {

enum class LogLevel { Debug = 0, Info = 1, Warn = 2, Error = 3 };

// Human-readable diagnostic log written to stderr. Thread-safe.
//   2026-10-10T16:40:01.123Z INFO  [laptop-1] claimed job-001 (attempt 1)
namespace log {

void set_level(LogLevel level);
void set_component(std::string component);

void debug(std::string_view message);
void info(std::string_view message);
void warn(std::string_view message);
void error(std::string_view message);

}  // namespace log
}  // namespace spot_runner
