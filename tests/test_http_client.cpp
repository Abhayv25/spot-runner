#include <gtest/gtest.h>

#include <chrono>
#include <thread>

#include "spot_runner/errors.h"
#include "spot_runner/http_client.h"
#include "spot_runner/util.h"
#include "test_http_server.h"

using namespace spot_runner;
using spot_runner::testing::RecordedRequest;
using spot_runner::testing::TestHttpServer;

TEST(HttpSerialize, WritesRequestLineAndManagedHeaders) {
    HttpRequest request;
    request.method = "POST";
    request.target = "/containers/create?name=x";
    request.headers["Content-Type"] = "application/json";
    request.body = R"({"Image":"a"})";

    const std::string text = serialize_request(request, "localhost");
    EXPECT_EQ(text.rfind("POST /containers/create?name=x HTTP/1.1\r\n", 0), 0U);
    EXPECT_NE(text.find("Host: localhost\r\n"), std::string::npos);
    EXPECT_NE(text.find("Content-Type: application/json\r\n"), std::string::npos);
    EXPECT_NE(text.find("Content-Length: 13\r\n"), std::string::npos);
    EXPECT_NE(text.find("Connection: close\r\n"), std::string::npos);
    EXPECT_EQ(text.substr(text.size() - 17), "\r\n\r\n" + request.body);
}

TEST(HttpSerialize, CallerCannotOverrideManagedHeaders) {
    HttpRequest request;
    request.headers["content-length"] = "999";
    request.headers["Connection"] = "keep-alive";
    const std::string text = serialize_request(request, "h");
    EXPECT_EQ(text.find("999"), std::string::npos);
    EXPECT_EQ(text.find("keep-alive"), std::string::npos);
}

TEST(HttpParse, ParsesStatusHeadersAndBody) {
    const auto r = parse_response("HTTP/1.1 201 Created\r\nContent-Type: x\r\nX-Thing:  y \r\n\r\nhello");
    EXPECT_EQ(r.status, 201);
    EXPECT_EQ(r.headers.at("content-type"), "x");
    EXPECT_EQ(r.headers.at("x-thing"), "y");
    EXPECT_EQ(r.body, "hello");
}

TEST(HttpParse, HonorsContentLength) {
    const auto r = parse_response("HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\nabcdef");
    EXPECT_EQ(r.body, "abc");
    EXPECT_THROW(parse_response("HTTP/1.1 200 OK\r\nContent-Length: 10\r\n\r\nabc"), HttpError);
}

TEST(HttpParse, DecodesChunkedBodies) {
    const auto r = parse_response(
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n"
        "5\r\nhello\r\n6;ext=1\r\n world\r\n1a\r\nabcdefghijklmnopqrstuvwxyz\r\n0\r\n\r\n");
    EXPECT_EQ(r.body, "hello worldabcdefghijklmnopqrstuvwxyz");
}

TEST(HttpParse, RejectsMalformedInput) {
    EXPECT_THROW(parse_response("HTTP/1.1 200 OK\r\nno blank line"), HttpError);
    EXPECT_THROW(parse_response("SMTP 220 hi\r\n\r\n"), HttpError);
    EXPECT_THROW(parse_response("HTTP/1.1 2x0 OK\r\n\r\n"), HttpError);
    EXPECT_THROW(parse_response("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\nzz\r\n"),
                 HttpError);
    EXPECT_THROW(parse_response("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n9\r\nabc"),
                 HttpError);
}

TEST(HttpClient, RoundTripOverUnixSocket) {
    const std::string path = "/tmp/sr-http-" + random_hex(4) + ".sock";
    TestHttpServer server(path, [](const RecordedRequest& req) {
        return TestHttpServer::response(200, "echo:" + req.method + " " + req.target + " " + req.body);
    });

    HttpRequest request;
    request.method = "PUT";
    request.target = "/things/1";
    request.body = "payload";
    const auto response = send_request(server.endpoint(), request, std::chrono::seconds(2));

    EXPECT_EQ(response.status, 200);
    EXPECT_EQ(response.body, "echo:PUT /things/1 payload");
    ASSERT_EQ(server.requests().size(), 1U);
    EXPECT_EQ(server.requests()[0].headers.at("host"), "localhost");
}

TEST(HttpClient, RoundTripOverTcpWithChunkedResponse) {
    TestHttpServer server([](const RecordedRequest&) {
        return std::string("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n3\r\nabc\r\n0\r\n\r\n");
    });
    const auto response = send_request(server.endpoint(), HttpRequest{}, std::chrono::seconds(2));
    EXPECT_EQ(response.body, "abc");
}

TEST(HttpClient, ReturnsErrorStatusesInsteadOfThrowing) {
    TestHttpServer server([](const RecordedRequest&) { return TestHttpServer::response(404, "nope"); });
    const auto response = send_request(server.endpoint(), HttpRequest{}, std::chrono::seconds(2));
    EXPECT_EQ(response.status, 404);
}

TEST(HttpClient, TimesOutWhenServerNeverAnswers) {
    TestHttpServer server([](const RecordedRequest&) {
        std::this_thread::sleep_for(std::chrono::milliseconds(800));
        return TestHttpServer::response(200, "late");
    });
    const auto start = std::chrono::steady_clock::now();
    EXPECT_THROW(send_request(server.endpoint(), HttpRequest{}, std::chrono::milliseconds(200)),
                 HttpError);
    EXPECT_LT(std::chrono::steady_clock::now() - start, std::chrono::milliseconds(700));
}

TEST(HttpClient, ConnectFailureThrows) {
    EXPECT_THROW(send_request(Endpoint::unix_socket("/tmp/sr-does-not-exist.sock"), HttpRequest{},
                              std::chrono::seconds(1)),
                 HttpError);
}

TEST(HttpClient, UrlEncodeKeepsUnreservedCharacters) {
    EXPECT_EQ(url_encode("a-b_c.d~e"), "a-b_c.d~e");
    EXPECT_EQ(url_encode(R"({"label":["x=y"]})"), "%7B%22label%22%3A%5B%22x%3Dy%22%5D%7D");
}
