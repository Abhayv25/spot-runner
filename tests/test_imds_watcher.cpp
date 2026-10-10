#include <gtest/gtest.h>

#include <atomic>
#include <cstdio>
#include <fstream>

#include "spot_runner/imds_watcher.h"
#include "spot_runner/util.h"
#include "test_http_server.h"

using namespace spot_runner;
using spot_runner::testing::RecordedRequest;
using spot_runner::testing::TestHttpServer;

namespace {

// Scripted IMDSv2: requires a token on every GET, like the real service.
struct FakeImds {
    std::atomic<bool> interruption{false};
    std::atomic<bool> rebalance{false};
    std::atomic<int> token_requests{0};

    std::string handle(const RecordedRequest& req) {
        if (req.method == "PUT" && req.target == "/latest/api/token") {
            if (req.headers.count("x-aws-ec2-metadata-token-ttl-seconds") == 0) {
                return TestHttpServer::response(400, "missing ttl");
            }
            ++token_requests;
            return TestHttpServer::response(200, "TOKEN-1");
        }
        const auto token = req.headers.find("x-aws-ec2-metadata-token");
        if (token == req.headers.end() || token->second != "TOKEN-1") {
            return TestHttpServer::response(401, "");
        }
        if (req.target == "/latest/meta-data/spot/instance-action") {
            return interruption ? TestHttpServer::response(
                                      200, R"({"action":"terminate","time":"2026-10-10T17:02:00Z"})")
                                : TestHttpServer::response(404, "");
        }
        if (req.target == "/latest/meta-data/events/recommendations/rebalance") {
            return rebalance ? TestHttpServer::response(200, R"({"noticeTime":"2026-10-10T17:00:00Z"})")
                             : TestHttpServer::response(404, "");
        }
        return TestHttpServer::response(404, "");
    }
};

}  // namespace

TEST(ImdsWatcher, NoNoticeWhenNothingScheduled) {
    FakeImds imds;
    TestHttpServer server([&](const RecordedRequest& r) { return imds.handle(r); });
    ImdsWatcher watcher(server.endpoint());
    EXPECT_FALSE(watcher.check().has_value());
}

TEST(ImdsWatcher, ReportsInterruptionNotice) {
    FakeImds imds;
    imds.interruption = true;
    TestHttpServer server([&](const RecordedRequest& r) { return imds.handle(r); });
    ImdsWatcher watcher(server.endpoint());
    const auto notice = watcher.check();
    ASSERT_TRUE(notice.has_value());
    EXPECT_EQ(notice->kind, InterruptionNotice::Kind::Interruption);
    EXPECT_EQ(notice->action, "terminate");
    EXPECT_EQ(notice->time, "2026-10-10T17:02:00Z");
}

TEST(ImdsWatcher, ReportsRebalanceRecommendation) {
    FakeImds imds;
    imds.rebalance = true;
    TestHttpServer server([&](const RecordedRequest& r) { return imds.handle(r); });
    ImdsWatcher watcher(server.endpoint());
    const auto notice = watcher.check();
    ASSERT_TRUE(notice.has_value());
    EXPECT_EQ(notice->kind, InterruptionNotice::Kind::RebalanceRecommendation);
}

TEST(ImdsWatcher, ReusesSessionToken) {
    FakeImds imds;
    TestHttpServer server([&](const RecordedRequest& r) { return imds.handle(r); });
    ImdsWatcher watcher(server.endpoint());
    for (int i = 0; i < 5; ++i) {
        watcher.check();
    }
    EXPECT_EQ(imds.token_requests.load(), 1);
}

TEST(ImdsWatcher, UnreachableServiceIsNotFatal) {
    // Nothing listens on this port: check() must log and return no notice, not throw.
    ImdsWatcher watcher(Endpoint::tcp("127.0.0.1", 1));
    EXPECT_FALSE(watcher.check().has_value());
}

TEST(FileWatcher, FiresWhenTriggerFileExists) {
    const std::string path = "/tmp/sr-trigger-" + random_hex(4);
    FileWatcher watcher(path);
    EXPECT_FALSE(watcher.check().has_value());

    { std::ofstream(path) << ""; }
    auto notice = watcher.check();
    ASSERT_TRUE(notice.has_value());
    EXPECT_EQ(notice->kind, InterruptionNotice::Kind::Interruption);

    { std::ofstream(path) << "rebalance\n"; }
    notice = watcher.check();
    ASSERT_TRUE(notice.has_value());
    EXPECT_EQ(notice->kind, InterruptionNotice::Kind::RebalanceRecommendation);
    std::remove(path.c_str());
}
