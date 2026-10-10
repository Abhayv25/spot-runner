#pragma once

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "spot_runner/http_client.h"

namespace spot_runner::testing {

struct RecordedRequest {
    std::string method;
    std::string target;
    HeaderMap headers;  // lowercase names
    std::string body;
};

// Raw HTTP response bytes to write back, then close the connection.
using Handler = std::function<std::string(const RecordedRequest&)>;

// Single-threaded HTTP/1.1 test server on a Unix socket or loopback TCP port.
// Each connection carries one request (the client always sends
// Connection: close). Used to test the HTTP client, the Docker client, and the
// IMDS watcher against scripted responses.
class TestHttpServer {
public:
    // Listens on a Unix socket at `unix_path`.
    TestHttpServer(const std::string& unix_path, Handler handler);
    // Listens on 127.0.0.1 on an ephemeral port.
    explicit TestHttpServer(Handler handler);
    ~TestHttpServer();

    TestHttpServer(const TestHttpServer&) = delete;
    TestHttpServer& operator=(const TestHttpServer&) = delete;

    Endpoint endpoint() const { return endpoint_; }
    std::vector<RecordedRequest> requests() const;

    static std::string response(int status, const std::string& body,
                                const std::string& extra_headers = "");

private:
    void serve();
    void handle_connection(int fd);

    int listen_fd_ = -1;
    Endpoint endpoint_;
    Handler handler_;
    std::atomic<bool> stopping_{false};
    std::thread thread_;
    mutable std::mutex mutex_;
    std::vector<RecordedRequest> requests_;
    std::string unix_path_;
};

}  // namespace spot_runner::testing
