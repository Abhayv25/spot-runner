#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace spot_runner {

// Milliseconds since the Unix epoch (wall clock). Used for timestamps that
// cross process boundaries: lease expiry, structured events.
std::int64_t unix_millis();

// "2026-10-10T16:40:01.123Z"
std::string format_iso8601(std::int64_t unix_ms);

// Atomically replaces `path` with `contents` (temp file + fsync + rename +
// directory fsync). Readers see the old file or the new one, never a mix.
void write_file_atomically(const std::filesystem::path& path, std::string_view contents);

// Throws std::runtime_error if the file cannot be read.
std::string read_file(const std::filesystem::path& path);

// Copies `from` to `to` atomically using the same temp-and-rename scheme.
void copy_file_atomically(const std::filesystem::path& from, const std::filesystem::path& to);

// Random lowercase hex string with `bytes` bytes of entropy.
std::string random_hex(std::size_t bytes);

std::string base64_encode(std::string_view input);

// Returns the last `max_lines` lines of `text`.
std::string tail_lines(std::string_view text, std::size_t max_lines);

std::optional<std::string> getenv_string(const char* name);

}  // namespace spot_runner
