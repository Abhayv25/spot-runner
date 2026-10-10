#include "spot_runner/logger.h"

#include <iostream>
#include <mutex>

#include "spot_runner/util.h"

namespace spot_runner::log {

namespace {

std::mutex g_mutex;
LogLevel g_level = LogLevel::Info;
std::string g_component;

std::string_view level_name(LogLevel level) {
    switch (level) {
        case LogLevel::Debug:
            return "DEBUG";
        case LogLevel::Info:
            return "INFO ";
        case LogLevel::Warn:
            return "WARN ";
        case LogLevel::Error:
            return "ERROR";
    }
    return "?????";
}

void write(LogLevel level, std::string_view message) {
    const std::string timestamp = format_iso8601(unix_millis());
    std::lock_guard<std::mutex> lock(g_mutex);
    if (level < g_level) {
        return;
    }
    std::cerr << timestamp << ' ' << level_name(level) << ' ';
    if (!g_component.empty()) {
        std::cerr << '[' << g_component << "] ";
    }
    // std::endl flushes; a log line still sitting in a buffer when the
    // process is killed is a log line that never existed.
    std::cerr << message << std::endl;
}

}  // namespace

void set_level(LogLevel level) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_level = level;
}

void set_component(std::string component) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_component = std::move(component);
}

void debug(std::string_view message) {
    write(LogLevel::Debug, message);
}
void info(std::string_view message) {
    write(LogLevel::Info, message);
}
void warn(std::string_view message) {
    write(LogLevel::Warn, message);
}
void error(std::string_view message) {
    write(LogLevel::Error, message);
}

}  // namespace spot_runner::log
