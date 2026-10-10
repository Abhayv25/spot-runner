#pragma once

#include <chrono>
#include <map>
#include <string>
#include <string_view>

namespace spot_runner {

// Minimal HTTP/1.1 client over POSIX sockets. It exists so the manager can
// talk to two local services without pulling in libcurl:
//   - the Docker Engine API, over the Unix socket /var/run/docker.sock
//   - the EC2 Instance Metadata Service, over TCP at 169.254.169.254:80
// Both are plain-HTTP, request/response services on a trusted local path, so
// every request uses "Connection: close" and the response ends at EOF. TLS,
// keep-alive, and redirects are intentionally out of scope.

struct Endpoint {
    enum class Kind { UnixSocket, Tcp };

    Kind kind = Kind::Tcp;
    std::string unix_path;  // Kind::UnixSocket
    std::string host;       // Kind::Tcp
    int port = 80;

    static Endpoint unix_socket(std::string path);
    static Endpoint tcp(std::string host, int port);
};

// Header names are stored lowercase; HTTP header names are case-insensitive.
using HeaderMap = std::map<std::string, std::string>;

struct HttpRequest {
    std::string method = "GET";
    std::string target = "/";  // path plus optional query string
    HeaderMap headers;
    std::string body;
};

struct HttpResponse {
    int status = 0;
    HeaderMap headers;
    std::string body;  // already de-chunked
};

// Serializes a request. Always sets Host, Content-Length, and Connection: close.
std::string serialize_request(const HttpRequest& request, std::string_view host_header);

// Parses a complete response (status line, headers, and body read to EOF).
// Decodes chunked transfer encoding and honors Content-Length. Throws HttpError
// on malformed input.
HttpResponse parse_response(std::string_view raw);

// Sends the request and returns the full response. `timeout` bounds the whole
// exchange (connect, send, and receive). Throws HttpError on transport errors;
// HTTP error statuses (4xx/5xx) are returned, not thrown.
HttpResponse send_request(const Endpoint& endpoint, const HttpRequest& request,
                          std::chrono::milliseconds timeout);

// Percent-encodes a query string component (RFC 3986 unreserved set kept).
std::string url_encode(std::string_view text);

}  // namespace spot_runner
