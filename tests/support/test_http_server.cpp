#include "test_http_server.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <stdexcept>

namespace spot_runner::testing {

namespace {

std::string lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

}  // namespace

TestHttpServer::TestHttpServer(const std::string& unix_path, Handler handler)
    : handler_(std::move(handler)), unix_path_(unix_path) {
    ::unlink(unix_path.c_str());
    listen_fd_ = ::socket(AF_UNIX, SOCK_STREAM, 0);
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, unix_path.c_str(), sizeof(addr.sun_path) - 1);
    if (::bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 ||
        ::listen(listen_fd_, 16) != 0) {
        throw std::runtime_error("test server: cannot listen on " + unix_path);
    }
    endpoint_ = Endpoint::unix_socket(unix_path);
    thread_ = std::thread([this] { serve(); });
}

TestHttpServer::TestHttpServer(Handler handler) : handler_(std::move(handler)) {
    listen_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    int one = 1;
    ::setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    if (::bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 ||
        ::listen(listen_fd_, 16) != 0) {
        throw std::runtime_error("test server: cannot listen on loopback");
    }
    socklen_t len = sizeof(addr);
    ::getsockname(listen_fd_, reinterpret_cast<sockaddr*>(&addr), &len);
    endpoint_ = Endpoint::tcp("127.0.0.1", ntohs(addr.sin_port));
    thread_ = std::thread([this] { serve(); });
}

TestHttpServer::~TestHttpServer() {
    stopping_ = true;
    thread_.join();
    ::close(listen_fd_);
    if (!unix_path_.empty()) {
        ::unlink(unix_path_.c_str());
    }
}

std::vector<RecordedRequest> TestHttpServer::requests() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return requests_;
}

std::string TestHttpServer::response(int status, const std::string& body,
                                     const std::string& extra_headers) {
    return "HTTP/1.1 " + std::to_string(status) + " X\r\nContent-Length: " +
           std::to_string(body.size()) + "\r\n" + extra_headers + "\r\n" + body;
}

void TestHttpServer::serve() {
    while (!stopping_) {
        pollfd pfd{listen_fd_, POLLIN, 0};
        if (::poll(&pfd, 1, 50) <= 0) {
            continue;
        }
        const int fd = ::accept(listen_fd_, nullptr, nullptr);
        if (fd >= 0) {
            handle_connection(fd);
            ::close(fd);
        }
    }
}

void TestHttpServer::handle_connection(int fd) {
    std::string data;
    char buffer[4096];
    std::size_t header_end = std::string::npos;
    std::size_t content_length = 0;
    while (true) {
        const ssize_t n = ::recv(fd, buffer, sizeof(buffer), 0);
        if (n <= 0) {
            return;
        }
        data.append(buffer, static_cast<std::size_t>(n));
        if (header_end == std::string::npos) {
            header_end = data.find("\r\n\r\n");
            if (header_end != std::string::npos) {
                const std::string head = lower(data.substr(0, header_end));
                const auto cl = head.find("content-length:");
                if (cl != std::string::npos) {
                    content_length = std::stoul(head.substr(cl + 15));
                }
            }
        }
        if (header_end != std::string::npos && data.size() >= header_end + 4 + content_length) {
            break;
        }
    }

    RecordedRequest request;
    const std::string head = data.substr(0, header_end);
    const auto line_end = head.find("\r\n");
    const std::string request_line = head.substr(0, line_end);
    const auto s1 = request_line.find(' ');
    const auto s2 = request_line.find(' ', s1 + 1);
    request.method = request_line.substr(0, s1);
    request.target = request_line.substr(s1 + 1, s2 - s1 - 1);
    std::size_t pos = line_end == std::string::npos ? head.size() : line_end + 2;
    while (pos < head.size()) {
        auto end = head.find("\r\n", pos);
        if (end == std::string::npos) {
            end = head.size();
        }
        const std::string line = head.substr(pos, end - pos);
        const auto colon = line.find(':');
        if (colon != std::string::npos) {
            std::string value = line.substr(colon + 1);
            value.erase(0, value.find_first_not_of(' '));
            request.headers[lower(line.substr(0, colon))] = value;
        }
        pos = end + 2;
    }
    request.body = data.substr(header_end + 4, content_length);

    {
        std::lock_guard<std::mutex> lock(mutex_);
        requests_.push_back(request);
    }
    const std::string reply = handler_(request);
    std::size_t sent = 0;
    while (sent < reply.size()) {
        const ssize_t n = ::send(fd, reply.data() + sent, reply.size() - sent, 0);
        if (n <= 0) {
            return;
        }
        sent += static_cast<std::size_t>(n);
    }
}

}  // namespace spot_runner::testing
