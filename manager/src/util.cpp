#include "spot_runner/util.h"

#include <fcntl.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <random>
#include <sstream>
#include <stdexcept>
#include <system_error>

namespace spot_runner {

namespace {

[[noreturn]] void throw_errno(const std::string& what) {
    throw std::system_error(errno, std::generic_category(), what);
}

class FileDescriptor {
public:
    explicit FileDescriptor(int fd) : fd_(fd) {}
    ~FileDescriptor() {
        if (fd_ >= 0) {
            ::close(fd_);
        }
    }
    FileDescriptor(const FileDescriptor&) = delete;
    FileDescriptor& operator=(const FileDescriptor&) = delete;

    int get() const { return fd_; }
    int release() {
        const int fd = fd_;
        fd_ = -1;
        return fd;
    }

private:
    int fd_;
};

void fsync_directory(const std::filesystem::path& dir) {
    FileDescriptor fd(::open(dir.c_str(), O_RDONLY | O_CLOEXEC));
    if (fd.get() >= 0) {
        // Best effort: some filesystems do not support fsync on directories.
        ::fsync(fd.get());
    }
}

}  // namespace

std::int64_t unix_millis() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

std::string format_iso8601(std::int64_t unix_ms) {
    const std::time_t seconds = static_cast<std::time_t>(unix_ms / 1000);
    const int millis = static_cast<int>(unix_ms % 1000);
    std::tm utc{};
    gmtime_r(&seconds, &utc);
    std::array<char, 32> buffer{};
    std::strftime(buffer.data(), buffer.size(), "%Y-%m-%dT%H:%M:%S", &utc);
    std::array<char, 40> out{};
    std::snprintf(out.data(), out.size(), "%s.%03dZ", buffer.data(), millis);
    return out.data();
}

void write_file_atomically(const std::filesystem::path& path, std::string_view contents) {
    const auto parent = path.has_parent_path() ? path.parent_path() : std::filesystem::path(".");
    std::filesystem::create_directories(parent);
    const std::filesystem::path temp = path.string() + ".tmp-" + random_hex(4);

    {
        FileDescriptor fd(::open(temp.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644));
        if (fd.get() < 0) {
            throw_errno("open " + temp.string());
        }
        std::size_t written = 0;
        while (written < contents.size()) {
            const ssize_t n = ::write(fd.get(), contents.data() + written, contents.size() - written);
            if (n < 0) {
                if (errno == EINTR) {
                    continue;
                }
                const int saved = errno;
                ::unlink(temp.c_str());
                errno = saved;
                throw_errno("write " + temp.string());
            }
            written += static_cast<std::size_t>(n);
        }
        if (::fsync(fd.get()) != 0) {
            const int saved = errno;
            ::unlink(temp.c_str());
            errno = saved;
            throw_errno("fsync " + temp.string());
        }
        if (::close(fd.release()) != 0) {
            throw_errno("close " + temp.string());
        }
    }

    if (::rename(temp.c_str(), path.c_str()) != 0) {
        const int saved = errno;
        ::unlink(temp.c_str());
        errno = saved;
        throw_errno("rename " + temp.string() + " -> " + path.string());
    }
    fsync_directory(parent);
}

std::string read_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("cannot read " + path.string());
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

void copy_file_atomically(const std::filesystem::path& from, const std::filesystem::path& to) {
    write_file_atomically(to, read_file(from));
}

std::string random_hex(std::size_t bytes) {
    static thread_local std::mt19937_64 engine{std::random_device{}()};
    static constexpr char kDigits[] = "0123456789abcdef";
    std::string out;
    out.reserve(bytes * 2);
    for (std::size_t i = 0; i < bytes; ++i) {
        const auto byte = static_cast<unsigned>(engine() & 0xffU);
        out.push_back(kDigits[byte >> 4U]);
        out.push_back(kDigits[byte & 0x0fU]);
    }
    return out;
}

std::string base64_encode(std::string_view input) {
    static constexpr char kAlphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((input.size() + 2) / 3) * 4);
    std::size_t i = 0;
    while (i + 3 <= input.size()) {
        const std::uint32_t chunk = (static_cast<std::uint8_t>(input[i]) << 16U) |
                                    (static_cast<std::uint8_t>(input[i + 1]) << 8U) |
                                    static_cast<std::uint8_t>(input[i + 2]);
        out.push_back(kAlphabet[(chunk >> 18U) & 0x3fU]);
        out.push_back(kAlphabet[(chunk >> 12U) & 0x3fU]);
        out.push_back(kAlphabet[(chunk >> 6U) & 0x3fU]);
        out.push_back(kAlphabet[chunk & 0x3fU]);
        i += 3;
    }
    const std::size_t remaining = input.size() - i;
    if (remaining > 0) {
        std::uint32_t chunk = static_cast<std::uint8_t>(input[i]) << 16U;
        if (remaining == 2) {
            chunk |= static_cast<std::uint8_t>(input[i + 1]) << 8U;
        }
        out.push_back(kAlphabet[(chunk >> 18U) & 0x3fU]);
        out.push_back(kAlphabet[(chunk >> 12U) & 0x3fU]);
        out.push_back(remaining == 2 ? kAlphabet[(chunk >> 6U) & 0x3fU] : '=');
        out.push_back('=');
    }
    return out;
}

std::string tail_lines(std::string_view text, std::size_t max_lines) {
    if (max_lines == 0 || text.empty()) {
        return {};
    }
    std::size_t end = text.size();
    if (text.back() == '\n') {
        --end;
    }
    std::size_t lines = 0;
    std::size_t pos = end;
    while (pos > 0) {
        if (text[pos - 1] == '\n') {
            if (++lines == max_lines) {
                break;
            }
        }
        --pos;
    }
    return std::string(text.substr(pos));
}

std::optional<std::string> getenv_string(const char* name) {
    const char* value = std::getenv(name);
    if (value == nullptr) {
        return std::nullopt;
    }
    return std::string(value);
}

}  // namespace spot_runner
