// prime-counter: reference workload for spot-runner.
//
// Counts the primes in [2, TARGET] by trial division. The computation is
// deliberately CPU-bound and slow so that it can be interrupted mid-run, and
// it implements the checkpoint contract described in docs/checkpoint-contract.md:
//
//   - On start, resume from $CHECKPOINT_DIR/checkpoint.dat if it exists.
//   - Autosave progress every AUTOSAVE_MS milliseconds (atomic write).
//   - On SIGTERM or SIGINT, write a final checkpoint and exit with code 75.
//   - On completion, write result.dat and exit with code 0.
//
// Environment:
//   JOB_ID           identifier used in log lines        (default "local")
//   CHECKPOINT_DIR   directory for checkpoint and result (default "./checkpoint")
//   TARGET           upper bound, inclusive              (default 100000000)
//   AUTOSAVE_MS      autosave interval in milliseconds   (default 5000)

#include <fcntl.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

namespace {

constexpr int kExitCompleted = 0;
constexpr int kExitError = 1;
constexpr int kExitInterrupted = 75;  // EX_TEMPFAIL: stopped early, safe to resume.

// How many candidates to test between clock reads. Reading the clock on every
// iteration would dominate the cost of testing small numbers.
constexpr std::int64_t kClockCheckStride = 4096;

volatile std::sig_atomic_t g_stop_requested = 0;

extern "C" void handle_stop_signal(int /*signal_number*/) {
    g_stop_requested = 1;
}

struct Progress {
    std::int64_t target = 0;
    std::int64_t next_number = 2;  // next candidate to test
    std::int64_t primes_found = 0;
    std::int64_t last_prime = 0;
};

struct Settings {
    std::string job_id;
    std::filesystem::path checkpoint_dir;
    std::int64_t target = 0;
    std::chrono::milliseconds autosave_interval{0};
};

std::string env_or(const char* name, std::string_view fallback) {
    const char* value = std::getenv(name);
    return value != nullptr ? std::string(value) : std::string(fallback);
}

std::optional<std::int64_t> parse_int64(const std::string& text) {
    try {
        std::size_t consumed = 0;
        const long long value = std::stoll(text, &consumed);
        if (consumed != text.size()) {
            return std::nullopt;
        }
        return static_cast<std::int64_t>(value);
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

bool is_prime(std::int64_t n) {
    if (n < 2) {
        return false;
    }
    if (n < 4) {
        return true;
    }
    if (n % 2 == 0) {
        return false;
    }
    for (std::int64_t divisor = 3; divisor <= n / divisor; divisor += 2) {
        if (n % divisor == 0) {
            return false;
        }
    }
    return true;
}

std::string serialize(const Progress& progress) {
    std::ostringstream out;
    out << "version=1\n"
        << "target=" << progress.target << '\n'
        << "next_number=" << progress.next_number << '\n'
        << "primes_found=" << progress.primes_found << '\n'
        << "last_prime=" << progress.last_prime << '\n';
    return out.str();
}

// Returns nullopt if the file is missing or malformed. A malformed checkpoint
// is treated as absent so the job restarts cleanly instead of crashing.
std::optional<Progress> load_checkpoint(const std::filesystem::path& path) {
    std::ifstream in(path);
    if (!in) {
        return std::nullopt;
    }

    Progress progress;
    bool has_target = false;
    bool has_next = false;
    bool has_found = false;
    bool has_last = false;

    std::string line;
    while (std::getline(in, line)) {
        const auto separator = line.find('=');
        if (separator == std::string::npos) {
            continue;
        }
        const std::string key = line.substr(0, separator);
        const auto value = parse_int64(line.substr(separator + 1));
        if (!value) {
            return std::nullopt;
        }
        if (key == "target") {
            progress.target = *value;
            has_target = true;
        } else if (key == "next_number") {
            progress.next_number = *value;
            has_next = true;
        } else if (key == "primes_found") {
            progress.primes_found = *value;
            has_found = true;
        } else if (key == "last_prime") {
            progress.last_prime = *value;
            has_last = true;
        }
    }

    if (!has_target || !has_next || !has_found || !has_last || progress.next_number < 2 ||
        progress.primes_found < 0) {
        return std::nullopt;
    }
    return progress;
}

// Writes `contents` to `path` so that a reader observes either the previous
// file or the new one, never a partial write: write a temp file in the same
// directory, fsync it, rename it over the target, then fsync the directory so
// the rename itself survives a crash.
bool write_file_atomically(const std::filesystem::path& path, const std::string& contents) {
    const std::filesystem::path temp_path = path.string() + ".tmp";

    const int fd = ::open(temp_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd < 0) {
        std::cerr << "open " << temp_path << ": " << std::strerror(errno) << '\n';
        return false;
    }

    std::size_t written = 0;
    while (written < contents.size()) {
        const ssize_t n = ::write(fd, contents.data() + written, contents.size() - written);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            std::cerr << "write " << temp_path << ": " << std::strerror(errno) << '\n';
            ::close(fd);
            return false;
        }
        written += static_cast<std::size_t>(n);
    }

    if (::fsync(fd) != 0 || ::close(fd) != 0) {
        std::cerr << "fsync/close " << temp_path << ": " << std::strerror(errno) << '\n';
        return false;
    }

    if (::rename(temp_path.c_str(), path.c_str()) != 0) {
        std::cerr << "rename " << temp_path << ": " << std::strerror(errno) << '\n';
        return false;
    }

    const int dir_fd = ::open(path.parent_path().c_str(), O_RDONLY | O_CLOEXEC);
    if (dir_fd >= 0) {
        ::fsync(dir_fd);
        ::close(dir_fd);
    }
    return true;
}

std::optional<Settings> read_settings() {
    Settings settings;
    settings.job_id = env_or("JOB_ID", "local");
    settings.checkpoint_dir = env_or("CHECKPOINT_DIR", "./checkpoint");

    const auto target = parse_int64(env_or("TARGET", "100000000"));
    const auto autosave_ms = parse_int64(env_or("AUTOSAVE_MS", "5000"));
    if (!target || *target < 2) {
        std::cerr << "TARGET must be an integer >= 2\n";
        return std::nullopt;
    }
    if (!autosave_ms || *autosave_ms <= 0) {
        std::cerr << "AUTOSAVE_MS must be a positive integer\n";
        return std::nullopt;
    }
    settings.target = *target;
    settings.autosave_interval = std::chrono::milliseconds(*autosave_ms);
    return settings;
}

void install_signal_handlers() {
    struct sigaction action {};
    action.sa_handler = handle_stop_signal;
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;  // no SA_RESTART: interrupted syscalls should return
    sigaction(SIGTERM, &action, nullptr);
    sigaction(SIGINT, &action, nullptr);
}

}  // namespace

int main() {
    install_signal_handlers();

    const auto settings = read_settings();
    if (!settings) {
        return kExitError;
    }
    const std::string& id = settings->job_id;

    std::error_code ec;
    std::filesystem::create_directories(settings->checkpoint_dir, ec);
    if (ec) {
        std::cerr << "[" << id << "] cannot create " << settings->checkpoint_dir << ": "
                  << ec.message() << '\n';
        return kExitError;
    }
    const auto checkpoint_path = settings->checkpoint_dir / "checkpoint.dat";
    const auto result_path = settings->checkpoint_dir / "result.dat";

    Progress progress;
    progress.target = settings->target;
    if (auto restored = load_checkpoint(checkpoint_path)) {
        if (restored->target != settings->target) {
            std::cerr << "[" << id << "] checkpoint is for TARGET=" << restored->target
                      << ", expected " << settings->target << "; refusing to resume\n";
            return kExitError;
        }
        progress = *restored;
        std::cout << "[" << id << "] resuming at " << progress.next_number << " ("
                  << progress.primes_found << " primes so far)" << std::endl;
    } else {
        std::cout << "[" << id << "] starting fresh, target " << settings->target << std::endl;
    }

    using Clock = std::chrono::steady_clock;
    auto last_save = Clock::now();

    while (progress.next_number <= progress.target) {
        if (g_stop_requested != 0) {
            if (!write_file_atomically(checkpoint_path, serialize(progress))) {
                return kExitError;
            }
            std::cout << "[" << id << "] stop requested, checkpoint saved at "
                      << progress.next_number << std::endl;
            return kExitInterrupted;
        }

        if (is_prime(progress.next_number)) {
            ++progress.primes_found;
            progress.last_prime = progress.next_number;
        }
        ++progress.next_number;

        if (progress.next_number % kClockCheckStride == 0 &&
            Clock::now() - last_save >= settings->autosave_interval) {
            if (!write_file_atomically(checkpoint_path, serialize(progress))) {
                return kExitError;
            }
            last_save = Clock::now();
        }
    }

    std::ostringstream result;
    result << "target=" << progress.target << '\n'
           << "primes_found=" << progress.primes_found << '\n'
           << "last_prime=" << progress.last_prime << '\n';
    if (!write_file_atomically(result_path, result.str()) ||
        !write_file_atomically(checkpoint_path, serialize(progress))) {
        return kExitError;
    }

    std::cout << "[" << id << "] done: " << progress.primes_found << " primes <= "
              << progress.target << std::endl;
    return kExitCompleted;
}
