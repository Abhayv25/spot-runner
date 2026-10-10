#include "spot_runner/http_client.h"

#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <cstring>

#include "spot_runner/errors.h"

namespace spot_runner {

namespace {

using Clock = std::chrono::steady_clock;

#ifdef MSG_NOSIGNAL
constexpr int kSendFlags = MSG_NOSIGNAL;
#else
constexpr int kSendFlags = 0;  // macOS: SIGPIPE is suppressed with SO_NOSIGPIPE below.
#endif

class Socket {
public:
    Socket() = default;
    explicit Socket(int fd) : fd_(fd) {}
    ~Socket() { reset(); }
    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;
    Socket(Socket&& other) noexcept : fd_(other.fd_) { other.fd_ = -1; }
    Socket& operator=(Socket&& other) noexcept {
        if (this != &other) {
            reset();
            fd_ = other.fd_;
            other.fd_ = -1;
        }
        return *this;
    }

    int get() const { return fd_; }
    void reset() {
        if (fd_ >= 0) {
            ::close(fd_);
            fd_ = -1;
        }
    }

private:
    int fd_ = -1;
};

std::string to_lower(std::string_view text) {
    std::string out(text);
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

std::string_view trim(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) {
        text.remove_prefix(1);
    }
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r')) {
        text.remove_suffix(1);
    }
    return text;
}

int remaining_ms(Clock::time_point deadline) {
    const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now());
    return static_cast<int>(std::max<long long>(0, left.count()));
}

[[noreturn]] void throw_errno(const std::string& what) {
    throw HttpError(what + ": " + std::strerror(errno));
}

void configure_socket(int fd) {
    ::fcntl(fd, F_SETFD, FD_CLOEXEC);
#ifdef SO_NOSIGPIPE
    int one = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
#endif
    const int flags = ::fcntl(fd, F_GETFL, 0);
    ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

// Waits for `events` on `fd` until the deadline. Throws on timeout.
void wait_for(int fd, short events, Clock::time_point deadline, const char* phase) {
    while (true) {
        pollfd pfd{fd, events, 0};
        const int rc = ::poll(&pfd, 1, remaining_ms(deadline));
        if (rc > 0) {
            return;
        }
        if (rc == 0) {
            throw HttpError(std::string("timed out during ") + phase);
        }
        if (errno != EINTR) {
            throw_errno(std::string("poll during ") + phase);
        }
    }
}

// Non-blocking connect so a dead endpoint (for example IMDS when not on EC2)
// fails within the timeout instead of blocking for the kernel's TCP timeout.
void connect_with_deadline(int fd, const sockaddr* addr, socklen_t len,
                           Clock::time_point deadline) {
    if (::connect(fd, addr, len) == 0) {
        return;
    }
    if (errno != EINPROGRESS && errno != EAGAIN) {
        throw_errno("connect");
    }
    wait_for(fd, POLLOUT, deadline, "connect");
    int error = 0;
    socklen_t error_len = sizeof(error);
    ::getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &error_len);
    if (error != 0) {
        errno = error;
        throw_errno("connect");
    }
}

Socket open_connection(const Endpoint& endpoint, Clock::time_point deadline) {
    if (endpoint.kind == Endpoint::Kind::UnixSocket) {
        sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        if (endpoint.unix_path.size() >= sizeof(addr.sun_path)) {
            throw HttpError("unix socket path too long: " + endpoint.unix_path);
        }
        std::memcpy(addr.sun_path, endpoint.unix_path.c_str(), endpoint.unix_path.size() + 1);

        Socket sock(::socket(AF_UNIX, SOCK_STREAM, 0));
        if (sock.get() < 0) {
            throw_errno("socket");
        }
        configure_socket(sock.get());
        connect_with_deadline(sock.get(), reinterpret_cast<const sockaddr*>(&addr), sizeof(addr),
                              deadline);
        return sock;
    }

    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* results = nullptr;
    const std::string port = std::to_string(endpoint.port);
    const int rc = ::getaddrinfo(endpoint.host.c_str(), port.c_str(), &hints, &results);
    if (rc != 0) {
        throw HttpError("resolve " + endpoint.host + ": " + ::gai_strerror(rc));
    }

    std::string last_error = "no addresses";
    for (addrinfo* ai = results; ai != nullptr; ai = ai->ai_next) {
        Socket sock(::socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol));
        if (sock.get() < 0) {
            last_error = std::strerror(errno);
            continue;
        }
        configure_socket(sock.get());
        try {
            connect_with_deadline(sock.get(), ai->ai_addr, ai->ai_addrlen, deadline);
            ::freeaddrinfo(results);
            return sock;
        } catch (const HttpError& e) {
            last_error = e.what();
        }
    }
    ::freeaddrinfo(results);
    throw HttpError("connect " + endpoint.host + ":" + port + ": " + last_error);
}

void send_all(int fd, std::string_view data, Clock::time_point deadline) {
    std::size_t sent = 0;
    while (sent < data.size()) {
        const ssize_t n = ::send(fd, data.data() + sent, data.size() - sent, kSendFlags);
        if (n > 0) {
            sent += static_cast<std::size_t>(n);
            continue;
        }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            wait_for(fd, POLLOUT, deadline, "send");
            continue;
        }
        if (n < 0 && errno == EINTR) {
            continue;
        }
        throw_errno("send");
    }
}

std::string receive_all(int fd, Clock::time_point deadline) {
    std::string data;
    std::array<char, 16384> buffer{};
    while (true) {
        const ssize_t n = ::recv(fd, buffer.data(), buffer.size(), 0);
        if (n > 0) {
            data.append(buffer.data(), static_cast<std::size_t>(n));
            continue;
        }
        if (n == 0) {
            return data;  // server closed the connection: response complete
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            wait_for(fd, POLLIN, deadline, "receive");
            continue;
        }
        if (errno == EINTR) {
            continue;
        }
        throw_errno("recv");
    }
}

std::string decode_chunked(std::string_view body) {
    std::string out;
    std::size_t pos = 0;
    while (true) {
        const std::size_t line_end = body.find("\r\n", pos);
        if (line_end == std::string_view::npos) {
            throw HttpError("malformed chunked body: missing size line");
        }
        std::string_view size_text = body.substr(pos, line_end - pos);
        // Chunk extensions (";name=value") are allowed and ignored.
        if (const auto semi = size_text.find(';'); semi != std::string_view::npos) {
            size_text = size_text.substr(0, semi);
        }
        size_text = trim(size_text);
        if (size_text.empty()) {
            throw HttpError("malformed chunked body: empty chunk size");
        }
        std::size_t size = 0;
        for (const char c : size_text) {
            if (!std::isxdigit(static_cast<unsigned char>(c))) {
                throw HttpError("malformed chunked body: bad chunk size");
            }
            size = size * 16 + static_cast<std::size_t>(
                                   std::isdigit(static_cast<unsigned char>(c))
                                       ? c - '0'
                                       : std::tolower(static_cast<unsigned char>(c)) - 'a' + 10);
        }
        pos = line_end + 2;
        if (size == 0) {
            return out;  // trailers, if any, are ignored
        }
        if (pos + size > body.size()) {
            throw HttpError("malformed chunked body: truncated chunk");
        }
        out.append(body.substr(pos, size));
        pos += size;
        if (body.substr(pos, 2) != "\r\n") {
            throw HttpError("malformed chunked body: missing chunk terminator");
        }
        pos += 2;
    }
}

}  // namespace

Endpoint Endpoint::unix_socket(std::string path) {
    Endpoint e;
    e.kind = Kind::UnixSocket;
    e.unix_path = std::move(path);
    return e;
}

Endpoint Endpoint::tcp(std::string host, int port) {
    Endpoint e;
    e.kind = Kind::Tcp;
    e.host = std::move(host);
    e.port = port;
    return e;
}

std::string serialize_request(const HttpRequest& request, std::string_view host_header) {
    std::string out;
    out.reserve(256 + request.body.size());
    out += request.method;
    out += ' ';
    out += request.target;
    out += " HTTP/1.1\r\nHost: ";
    out += host_header;
    out += "\r\n";
    for (const auto& [name, value] : request.headers) {
        const std::string lower = to_lower(name);
        if (lower == "host" || lower == "content-length" || lower == "connection") {
            continue;  // managed below
        }
        out += name;
        out += ": ";
        out += value;
        out += "\r\n";
    }
    out += "Content-Length: ";
    out += std::to_string(request.body.size());
    out += "\r\nConnection: close\r\n\r\n";
    out += request.body;
    return out;
}

HttpResponse parse_response(std::string_view raw) {
    const std::size_t header_end = raw.find("\r\n\r\n");
    if (header_end == std::string_view::npos) {
        throw HttpError("malformed response: no end of headers");
    }
    const std::string_view head = raw.substr(0, header_end);
    std::string_view body = raw.substr(header_end + 4);

    HttpResponse response;

    const std::size_t status_end = head.find("\r\n");
    const std::string_view status_line = head.substr(0, status_end);
    if (status_line.substr(0, 5) != "HTTP/") {
        throw HttpError("malformed response: bad status line");
    }
    const std::size_t first_space = status_line.find(' ');
    if (first_space == std::string_view::npos || first_space + 4 > status_line.size()) {
        throw HttpError("malformed response: bad status line");
    }
    const std::string_view code = status_line.substr(first_space + 1, 3);
    if (!std::all_of(code.begin(), code.end(),
                     [](char c) { return std::isdigit(static_cast<unsigned char>(c)); })) {
        throw HttpError("malformed response: non-numeric status");
    }
    response.status = std::stoi(std::string(code));

    if (status_end != std::string_view::npos) {
        std::size_t pos = status_end + 2;
        while (pos < head.size()) {
            std::size_t line_end = head.find("\r\n", pos);
            if (line_end == std::string_view::npos) {
                line_end = head.size();
            }
            const std::string_view line = head.substr(pos, line_end - pos);
            const std::size_t colon = line.find(':');
            if (colon != std::string_view::npos) {
                response.headers[to_lower(trim(line.substr(0, colon)))] =
                    std::string(trim(line.substr(colon + 1)));
            }
            pos = line_end + 2;
        }
    }

    const auto te = response.headers.find("transfer-encoding");
    if (te != response.headers.end() && to_lower(te->second).find("chunked") != std::string::npos) {
        response.body = decode_chunked(body);
        return response;
    }

    const auto cl = response.headers.find("content-length");
    if (cl != response.headers.end()) {
        std::size_t length = 0;
        try {
            length = static_cast<std::size_t>(std::stoull(cl->second));
        } catch (const std::exception&) {
            throw HttpError("malformed response: bad Content-Length");
        }
        if (length > body.size()) {
            throw HttpError("truncated response body");
        }
        body = body.substr(0, length);
    }
    response.body = std::string(body);
    return response;
}

HttpResponse send_request(const Endpoint& endpoint, const HttpRequest& request,
                          std::chrono::milliseconds timeout) {
    const auto deadline = Clock::now() + timeout;
    Socket sock = open_connection(endpoint, deadline);
    const std::string host =
        endpoint.kind == Endpoint::Kind::UnixSocket ? std::string("localhost") : endpoint.host;
    send_all(sock.get(), serialize_request(request, host), deadline);
    return parse_response(receive_all(sock.get(), deadline));
}

std::string url_encode(std::string_view text) {
    static constexpr char kHex[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(text.size() * 3);
    for (const char c : text) {
        const auto u = static_cast<unsigned char>(c);
        if (std::isalnum(u) || c == '-' || c == '_' || c == '.' || c == '~') {
            out.push_back(c);
        } else {
            out.push_back('%');
            out.push_back(kHex[u >> 4U]);
            out.push_back(kHex[u & 0x0fU]);
        }
    }
    return out;
}

}  // namespace spot_runner
