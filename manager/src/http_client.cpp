// =============================================================================
// http_client.cpp: raw HTTP/1.1 over sockets (PHASE 5)
// =============================================================================
// Write and TEST build_request_text and parse_response_text first (pure string
// work, tests/test_http_parsing.cpp). Then do the socket part.
//
// STEP 1: Includes
//   "spot_runner/http_client.h", <sstream>, <algorithm>, <cctype>, <cstring>
//   POSIX socket headers (work on Mac and Linux):
//     <sys/socket.h>  socket, connect, send, recv
//     <sys/un.h>      sockaddr_un (Unix sockets)
//     <netdb.h>       getaddrinfo (turn a host name into an address)
//     <unistd.h>      close
//     <poll.h>        poll (for timeouts)
//     <cerrno>        errno
//   using namespace std;  namespace spot_runner { ... }
//
// STEP 2: unix_endpoint / tcp_endpoint: just fill in an Endpoint and return it.
//
// STEP 3: build_request_text(request, host_header)
//   HTTP is just text. A request looks EXACTLY like this (\r\n line endings!):
//       POST /containers/create HTTP/1.1\r\n
//       Host: localhost\r\n
//       Content-Type: application/json\r\n
//       Content-Length: 57\r\n
//       Connection: close\r\n
//       \r\n
//       {"Image":"prime-counter:latest", ...}
//   - First line: method + " " + path + " HTTP/1.1\r\n"
//   - "Host: " + host_header. (Docker over a Unix socket accepts "localhost".)
//   - Every header from request.headers.
//   - Always add "Content-Length" (= body.size(), even if 0) and "Connection: close".
//     Connection: close = the server hangs up after replying, so "read until the
//     connection closes" tells you the response is complete. Simplest correct design.
//   - A blank line ("\r\n"), then the body.
//
// STEP 4: parse_response_text(raw)
//   4a. Find the first "\r\n\r\n". Before it = status line + headers. After = body.
//       If it's missing, throw HttpError("malformed response").
//   4b. Status line: "HTTP/1.1 200 OK". Take the number between the first two spaces.
//   4c. Each header line "Name: value": split on the first ':', trim spaces,
//       lowercase the name (headers are case-insensitive; "Content-Type" and
//       "content-type" are the same header).
//   4d. Body: if headers["transfer-encoding"] == "chunked", DE-CHUNK it:
//         Chunked format is repeated blocks of:  <size in HEX>\r\n<that many bytes>\r\n
//         ending with a size of 0. Example:
//             5\r\nhello\r\n6\r\n world\r\n0\r\n\r\n   ->   "hello world"
//         Loop: read hex size (stoul(text, nullptr, 16)), stop at 0, append bytes,
//         skip the trailing \r\n. Docker uses chunked a lot, so this matters!
//       Otherwise the body is everything after the blank line.
//
// STEP 5: A private helper: int connect_socket(const Endpoint& endpoint)
//   Unix socket:
//     - fd = socket(AF_UNIX, SOCK_STREAM, 0)
//     - sockaddr_un addr{}; addr.sun_family = AF_UNIX;
//       copy the path into addr.sun_path (check it fits! it's ~104 bytes on Mac)
//     - connect(fd, (sockaddr*)&addr, sizeof(addr))
//   TCP:
//     - getaddrinfo(host, to_string(port), hints with SOCK_STREAM, &result)
//     - socket(...) and connect(...) with the first result; freeaddrinfo after.
//   On any failure: close(fd) if opened, throw HttpError including strerror(errno).
//   Return fd.
//   Note: the IMDS address only answers inside EC2. Elsewhere connect may hang,
//   so give connect a timeout too (non-blocking connect + poll, or just rely on
//   the overall timeout for now and improve it later).
//
// STEP 6: send_request(endpoint, request, timeout_ms)
//   6a. fd = connect_socket(endpoint)
//   6b. text = build_request_text(request, endpoint.use_unix_socket ? "localhost" : endpoint.host)
//   6c. SEND ALL: send() may send fewer bytes than you asked. Loop until every byte
//       is sent. (Classic bug. Interviewers love it.)
//   6d. RECEIVE ALL: loop:
//         poll(fd for POLLIN, timeout_ms). 0 = timed out -> close, throw HttpError.
//         n = recv(fd, buffer, sizeof(buffer), 0)
//         n > 0: append to a string.  n == 0: server closed = done.  n < 0: error.
//   6e. close(fd) on EVERY path (success and errors). Tip: a tiny RAII struct whose
//       destructor calls close() makes this automatic. Look up "RAII" and use it;
//       it's a C++ interview favorite.
//   6f. return parse_response_text(received)
//
// STEP 7: Try it by hand before DockerClient exists: in a scratch main, send
//   GET /_ping to "/var/run/docker.sock" (Docker Desktop running). Body should be "OK".
//   Compare with:  curl --unix-socket /var/run/docker.sock http://localhost/_ping
//   (If that file doesn't exist on your Mac, try ~/.docker/run/docker.sock, or turn on
//    "Allow the default Docker socket" in Docker Desktop's advanced settings.)
// =============================================================================
