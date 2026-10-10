#include <gtest/gtest.h>

#include "spot_runner/config.h"
#include "spot_runner/errors.h"

using spot_runner::Backend;
using spot_runner::ConfigError;
using spot_runner::parse_config;
using spot_runner::RuntimeKind;

namespace {

const char* kLocalConfig = R"({
  "backend": "local",
  "runtime": "process",
  "manager_id": "m-1",
  "work_dir": "/tmp/work",
  "local": {"storage_dir": "/tmp/s", "queue_dir": "/tmp/q", "interrupt_file": "/tmp/i"}
})";

std::string with(const std::string& extra_fields) {
    return std::string(R"({"manager_id": "m-1", "work_dir": "/tmp/w",
        "local": {"storage_dir": "s", "queue_dir": "q", "interrupt_file": "i"}, )") +
           extra_fields + "}";
}

}  // namespace

TEST(Config, ParsesLocalConfigWithDefaults) {
    const auto config = parse_config(kLocalConfig);
    EXPECT_EQ(config.backend, Backend::Local);
    EXPECT_EQ(config.runtime, RuntimeKind::Process);
    EXPECT_EQ(config.manager_id, "m-1");
    EXPECT_EQ(config.local.queue_dir, "/tmp/q");
    EXPECT_EQ(config.max_attempts, 3);
    EXPECT_EQ(config.timing.lease_duration, std::chrono::seconds(60));
    EXPECT_TRUE(config.drain_on_rebalance);
}

TEST(Config, ParsesTimingOverrides) {
    const auto config = parse_config(
        with(R"("timing": {"poll_interval_ms": 100, "lease_duration_s": 10, "stop_grace_period_s": 5})"));
    EXPECT_EQ(config.timing.poll_interval, std::chrono::milliseconds(100));
    EXPECT_EQ(config.timing.lease_duration, std::chrono::seconds(10));
    EXPECT_EQ(config.timing.stop_grace_period, std::chrono::seconds(5));
}

TEST(Config, ParsesAwsSection) {
    const auto config = parse_config(R"({
      "backend": "aws", "work_dir": "/w",
      "aws": {"region": "us-east-1", "bucket": "b", "queue_url": "q", "dead_letter_queue_url": "d"}
    })");
    EXPECT_EQ(config.backend, Backend::Aws);
    EXPECT_EQ(config.aws.bucket, "b");
    EXPECT_EQ(config.aws.key_prefix, "spot-runner/");
    EXPECT_EQ(config.manager_id, "auto");
}

TEST(Config, RejectsInvalidJson) {
    EXPECT_THROW(parse_config("{ not json"), ConfigError);
    EXPECT_THROW(parse_config("[1, 2]"), ConfigError);
}

TEST(Config, RejectsUnknownBackendAndRuntime) {
    EXPECT_THROW(parse_config(with(R"("backend": "gcp")")), ConfigError);
    EXPECT_THROW(parse_config(with(R"("runtime": "podman")")), ConfigError);
}

TEST(Config, RequiresBackendSection) {
    EXPECT_THROW(parse_config(R"({"backend": "local", "work_dir": "/w"})"), ConfigError);
    EXPECT_THROW(parse_config(R"({"backend": "aws", "work_dir": "/w"})"), ConfigError);
}

TEST(Config, RequiresWorkDir) {
    EXPECT_THROW(parse_config(R"({"local": {"storage_dir": "s", "queue_dir": "q",
        "interrupt_file": "i"}})"),
                 ConfigError);
}

TEST(Config, RejectsGracePeriodThatDoesNotFitInSpotNotice) {
    // 110 s of grace leaves no time to upload inside the 120 s notice window.
    EXPECT_THROW(parse_config(with(R"("timing": {"stop_grace_period_s": 110})")), ConfigError);
    EXPECT_NO_THROW(parse_config(with(R"("timing": {"stop_grace_period_s": 100})")));
}

TEST(Config, RejectsLeaseTooShortForPollInterval) {
    EXPECT_THROW(
        parse_config(with(R"("timing": {"poll_interval_ms": 1000, "lease_duration_s": 3})")),
        ConfigError);
}

TEST(Config, RejectsWrongTypes) {
    EXPECT_THROW(parse_config(with(R"("max_attempts": "three")")), ConfigError);
    EXPECT_THROW(parse_config(with(R"("max_attempts": 0)")), ConfigError);
}

TEST(Config, MissingFileThrows) {
    EXPECT_THROW(spot_runner::load_config("/nonexistent/config.json"), ConfigError);
}
