// =============================================================================
// test_http_parsing.cpp (PHASE 5)
// =============================================================================
// Tests the pure-string half of http_client (no sockets, no Docker needed).
// Include <gtest/gtest.h> and "spot_runner/http_client.h"; using namespace spot_runner.
//
// TESTS TO WRITE:
//   build_request_text:
//   1. GetHasCorrectFirstLine: starts with "GET /_ping HTTP/1.1\r\n"
//   2. AlwaysHasHostLengthAndClose: contains "Host: localhost\r\n",
//      "Content-Length: 0\r\n", "Connection: close\r\n"
//   3. PostIncludesBodyAfterBlankLine: ends with "\r\n\r\n" + body; Content-Length right
//
//   parse_response_text:
//   4. ParsesStatusCode: "HTTP/1.1 404 Not Found\r\n\r\n" -> 404
//   5. HeadersAreLowercased: "Content-Type: x" -> headers["content-type"] == "x"
//   6. PlainBody: body after the blank line is returned exactly
//   7. ChunkedBody: "Transfer-Encoding: chunked" with
//        "5\r\nhello\r\n6\r\n world\r\n0\r\n\r\n" -> "hello world"
//   8. ChunkedWithHexSizes: a chunk of size "1a" (26 bytes) works
//   9. MalformedThrows: text with no "\r\n\r\n" -> EXPECT_THROW(..., HttpError)
//
// Want a challenge later? Write an integration test that starts a tiny server on a
// Unix socket in a background thread and calls send_request against it.
// =============================================================================
