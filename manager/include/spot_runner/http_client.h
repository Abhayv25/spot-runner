// =============================================================================
// http_client.h: a tiny HTTP/1.1 client written from scratch (PHASE 5)
// =============================================================================
// This is the most "systems" file in the project. You'll talk raw HTTP over sockets.
// Two things use it:
//   1. DockerClient: Docker listens on a Unix socket (a file like /var/run/docker.sock)
//   2. ImdsWatcher:  Amazon's metadata service is plain TCP at 169.254.169.254:80
// Same HTTP text either way, just a different kind of socket underneath.
//
// STEP 1: #pragma once, includes <string>, <map>, <stdexcept>, namespace spot_runner
//
// STEP 2: struct HttpRequest
//   std::string method;                          // "GET", "POST", "PUT", "DELETE"
//   std::string path;                            // "/containers/json?all=1"
//   std::map<std::string, std::string> headers;
//   std::string body;                            // usually JSON, may be empty
//
// STEP 3: struct HttpResponse
//   int status_code = 0;                         // 200, 404, ...
//   std::map<std::string, std::string> headers;  // store keys in lowercase!
//   std::string body;                            // already de-chunked (see .cpp)
//
// STEP 4: struct Endpoint (where to connect)
//   bool use_unix_socket = false;
//   std::string unix_socket_path;                // when use_unix_socket is true
//   std::string host;                            // when false, e.g. "169.254.169.254"
//   int port = 80;
//   Add two small helper prototypes that build one:
//     Endpoint unix_endpoint(const std::string& socket_path);
//     Endpoint tcp_endpoint(const std::string& host, int port);
//
// STEP 5: struct HttpError : public std::runtime_error
//   (Inherit the constructor: using std::runtime_error::runtime_error;)
//   Throw this for connection failures, timeouts, and bad responses.
//
// STEP 6: Prototypes
//   HttpResponse send_request(const Endpoint& endpoint, const HttpRequest& request,
//                             int timeout_ms);
//
//   These two are separate on purpose so tests can check them with NO network:
//   std::string build_request_text(const HttpRequest& request, const std::string& host_header);
//   HttpResponse parse_response_text(const std::string& raw);
// =============================================================================
