#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include "spot_runner/docker_client.h"
#include "spot_runner/errors.h"
#include "spot_runner/util.h"
#include "test_http_server.h"

using namespace spot_runner;
using nlohmann::json;
using spot_runner::testing::RecordedRequest;
using spot_runner::testing::TestHttpServer;

namespace {

// Scripted subset of the Docker Engine API.
std::string fake_docker(const RecordedRequest& req) {
    const auto& t = req.target;
    if (req.method == "GET" && t == "/_ping") {
        return TestHttpServer::response(200, "OK");
    }
    if (req.method == "GET" && t == "/images/present:1/json") {
        return TestHttpServer::response(200, "{}");
    }
    if (req.method == "GET" && t == "/images/missing:2/json") {
        return TestHttpServer::response(404, R"({"message":"no such image"})");
    }
    if (req.method == "POST" && t.rfind("/images/create?fromImage=missing&tag=2", 0) == 0) {
        return TestHttpServer::response(200, "{\"status\":\"Pulling\"}\n{\"status\":\"Done\"}\n");
    }
    if (req.method == "POST" && t.rfind("/images/create?fromImage=broken", 0) == 0) {
        return TestHttpServer::response(200, "{\"status\":\"Pulling\"}\n{\"error\":\"denied\"}\n");
    }
    if (req.method == "GET" && t.rfind("/images/broken", 0) == 0) {
        return TestHttpServer::response(404, "{}");
    }
    if (req.method == "DELETE") {
        return TestHttpServer::response(404, R"({"message":"no such container"})");
    }
    if (req.method == "POST" && t.rfind("/containers/create?name=spot-runner-job-1", 0) == 0) {
        return TestHttpServer::response(201, R"({"Id":"abc123def456789","Warnings":[]})");
    }
    if (req.method == "POST" && t == "/containers/abc123def456789/start") {
        return "HTTP/1.1 204 No Content\r\n\r\n";
    }
    if (req.method == "GET" && t == "/containers/abc123def456789/json") {
        return TestHttpServer::response(200, R"({"State":{"Running":false,"ExitCode":75}})");
    }
    if (req.method == "GET" && t == "/containers/gone/json") {
        return TestHttpServer::response(404, R"({"message":"no such container"})");
    }
    if (req.method == "POST" && t == "/containers/abc123def456789/stop?t=30") {
        return "HTTP/1.1 204 No Content\r\n\r\n";
    }
    return TestHttpServer::response(500, R"({"message":"unexpected request )" + req.method + " " + t + "\"}");
}

class DockerClientTest : public ::testing::Test {
protected:
    std::string path_ = "/tmp/sr-docker-" + random_hex(4) + ".sock";
    TestHttpServer server_{path_, fake_docker};
    DockerClient docker_{path_};
};

}  // namespace

TEST_F(DockerClientTest, Ping) {
    EXPECT_TRUE(docker_.ping());
    EXPECT_FALSE(DockerClient("/tmp/sr-nothing-here.sock").ping());
}

TEST_F(DockerClientTest, EnsureImageSkipsPullWhenPresent) {
    docker_.ensure_image("present:1");
    ASSERT_EQ(server_.requests().size(), 1U);
}

TEST_F(DockerClientTest, EnsureImagePullsWhenMissing) {
    docker_.ensure_image("missing:2");
    const auto reqs = server_.requests();
    ASSERT_EQ(reqs.size(), 2U);
    EXPECT_EQ(reqs[1].method, "POST");
}

TEST_F(DockerClientTest, PullErrorInStreamIsReported) {
    EXPECT_THROW(docker_.ensure_image("broken:1"), RuntimeError);
}

TEST_F(DockerClientTest, StartCreatesThenStartsContainer) {
    ContainerSpec spec;
    spec.image = "prime-counter:latest";
    spec.name = "spot-runner-job-1";
    spec.env = {{"TARGET", "1000"}};
    spec.labels = {{"spot-runner.manager", "m-1"}};
    spec.checkpoint_dir = "/var/lib/spot-runner/work/job-1";

    EXPECT_EQ(docker_.start(spec), "abc123def456789");

    const auto reqs = server_.requests();
    ASSERT_EQ(reqs.size(), 3U);
    EXPECT_EQ(reqs[0].method, "DELETE");  // clears a leftover container with the same name
    EXPECT_EQ(reqs[1].method, "POST");
    EXPECT_EQ(reqs[2].target, "/containers/abc123def456789/start");

    const auto body = json::parse(reqs[1].body);
    EXPECT_EQ(body["Image"], "prime-counter:latest");
    EXPECT_EQ(body["Labels"]["spot-runner.manager"], "m-1");
    EXPECT_EQ(body["HostConfig"]["Binds"][0], "/var/lib/spot-runner/work/job-1:/checkpoint");
    const auto env = body["Env"].get<std::vector<std::string>>();
    EXPECT_NE(std::find(env.begin(), env.end(), "TARGET=1000"), env.end());
    EXPECT_NE(std::find(env.begin(), env.end(), "CHECKPOINT_DIR=/checkpoint"), env.end());
    EXPECT_TRUE(body.contains("User"));
}

TEST_F(DockerClientTest, InspectReportsExitCodeAndMissingContainers) {
    const auto status = docker_.inspect("abc123def456789");
    EXPECT_TRUE(status.exists);
    EXPECT_FALSE(status.running);
    EXPECT_EQ(status.exit_code, 75);
    EXPECT_FALSE(docker_.inspect("gone").exists);
}

TEST_F(DockerClientTest, StopPassesGracePeriod) {
    docker_.stop("abc123def456789", std::chrono::seconds(30));
    EXPECT_EQ(server_.requests().back().target, "/containers/abc123def456789/stop?t=30");
}

TEST(DockerLogs, DemultiplexesStreamFrames) {
    std::string raw;
    const auto frame = [&](char stream, const std::string& text) {
        raw.push_back(stream);
        raw.append(3, '\0');
        const auto n = static_cast<std::uint32_t>(text.size());
        raw.push_back(static_cast<char>((n >> 24U) & 0xffU));
        raw.push_back(static_cast<char>((n >> 16U) & 0xffU));
        raw.push_back(static_cast<char>((n >> 8U) & 0xffU));
        raw.push_back(static_cast<char>(n & 0xffU));
        raw += text;
    };
    frame(1, "out line\n");
    frame(2, "err line\n");
    EXPECT_EQ(DockerClient::demultiplex_logs(raw), "out line\nerr line\n");
    EXPECT_EQ(DockerClient::demultiplex_logs("plain tty output"), "plain tty output");
}
