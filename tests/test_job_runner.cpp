// JobRunner unit tests: real LocalStorage and LocalQueue on temp directories,
// fake container runtime and interruption watcher. No Docker, no network, and
// each test runs in well under a second.

#include <gtest/gtest.h>

#include <thread>

#include "fakes.h"
#include "spot_runner/event_log.h"
#include "spot_runner/job_runner.h"
#include "spot_runner/local_queue.h"
#include "spot_runner/local_storage.h"
#include "spot_runner/util.h"
#include "temp_dir.h"

using namespace spot_runner;
using spot_runner::testing::FakeRuntime;
using spot_runner::testing::FakeWatcher;
using spot_runner::testing::TempDir;
using spot_runner::testing::write_text;
namespace fs = std::filesystem;

namespace {

class JobRunnerTest : public ::testing::Test {
protected:
    JobRunnerTest() {
        config_.manager_id = "test-manager";
        config_.work_dir = dir_.str("work");
        config_.exit_when_idle = true;
        config_.max_attempts = 3;
        config_.timing.poll_interval = std::chrono::milliseconds(5);
        config_.timing.checkpoint_sync_interval = std::chrono::milliseconds(1);
        config_.timing.lease_duration = std::chrono::seconds(30);
        config_.timing.stop_grace_period = std::chrono::seconds(1);
        config_.timing.idle_backoff = std::chrono::milliseconds(5);
    }

    void push(const std::string& id, int attempts = 0) {
        Job job;
        job.job_id = id;
        job.image = "fake:latest";
        job.env = {{"TARGET", "100"}};
        job.attempts = attempts;
        queue_.push(job);
    }

    RunnerStats run(int max_jobs = 0) {
        JobRunner runner(config_, "test-manager", storage_, queue_, runtime_, watcher_, events_);
        return runner.run(max_jobs);
    }

    std::string stored(const std::string& key) {
        const std::string out = dir_.str("fetched-" + random_hex(4));
        return storage_.get_file(key, out) ? read_file(out) : std::string("<missing>");
    }

    bool queue_dir_has(const std::string& sub, const std::string& id) {
        return fs::exists(dir_.path() / "queue" / sub / (id + ".json"));
    }

    TempDir dir_;
    Config config_;
    LocalStorage storage_{dir_.str("storage")};
    LocalQueue queue_{dir_.str("queue")};
    FakeRuntime runtime_;
    FakeWatcher watcher_;
    EventLog events_{dir_.str("events.jsonl"), "test-manager"};
};

}  // namespace

TEST_F(JobRunnerTest, CompletedJobUploadsResultAndLeavesQueue) {
    push("job-1");
    runtime_.on_start = [](const fs::path& dir) {
        write_text(dir / "checkpoint.dat", "progress=100");
        write_text(dir / "result.dat", "primes=25");
    };

    const auto stats = run();

    EXPECT_EQ(stats.completed, 1);
    EXPECT_EQ(stored(result_key("job-1")), "primes=25");
    EXPECT_FALSE(queue_dir_has("pending", "job-1"));
    EXPECT_FALSE(queue_dir_has("inflight", "job-1"));
    EXPECT_FALSE(fs::exists(dir_.path() / "work" / "job-1"));  // work dir cleaned up
}

TEST_F(JobRunnerTest, ExitZeroWithoutResultIsAFailure) {
    push("job-1");
    const auto stats = run(1);
    EXPECT_EQ(stats.failed, 1);
    EXPECT_TRUE(queue_dir_has("pending", "job-1"));
}

TEST_F(JobRunnerTest, InterruptionStopsJobUploadsCheckpointAndRequeues) {
    push("job-1");
    runtime_.polls_until_exit = 1000;
    watcher_.fire_interrupt_after_checks = 3;
    runtime_.on_stop = [](const fs::path& dir) { write_text(dir / "checkpoint.dat", "final"); };

    const auto stats = run();

    EXPECT_TRUE(stats.received_interruption);
    EXPECT_EQ(stats.interrupted, 1);
    EXPECT_EQ(runtime_.stop_calls, 1);
    EXPECT_EQ(stored(checkpoint_key("job-1")), "final");

    // Back on the queue, and the interruption did not count as a failed attempt.
    LocalQueue queue(dir_.str("queue"));
    const auto requeued = queue.receive(std::chrono::seconds(5));
    ASSERT_TRUE(requeued.has_value());
    EXPECT_EQ(requeued->attempts, 0);
}

TEST_F(JobRunnerTest, ManagerStopsTakingWorkAfterInterruption) {
    push("job-1");
    push("job-2");
    runtime_.polls_until_exit = 1000;
    watcher_.fire_interrupt_after_checks = 2;
    run();
    EXPECT_EQ(runtime_.started.size(), 1U);
}

TEST_F(JobRunnerTest, ResumedJobSeesCheckpointBeforeItStarts) {
    push("job-1");
    const std::string src = dir_.str("seed");
    write_text(src, "saved-progress");
    storage_.put_file(checkpoint_key("job-1"), src);
    runtime_.on_start = [](const fs::path& dir) { write_text(dir / "result.dat", "done"); };

    run();

    EXPECT_TRUE(runtime_.checkpoint_present_at_start);
}

TEST_F(JobRunnerTest, PeriodicSyncUploadsCheckpointsWhileRunning) {
    push("job-1");
    runtime_.polls_until_exit = 6;
    runtime_.on_poll = [](const fs::path& dir, int poll) {
        write_text(dir / "checkpoint.dat", "step-" + std::to_string(poll));
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        if (poll == 5) {
            write_text(dir / "result.dat", "done");
        }
    };

    const auto stats = run();

    EXPECT_GE(stats.checkpoints_uploaded, 3);
    EXPECT_EQ(stats.completed, 1);
}

TEST_F(JobRunnerTest, UnchangedCheckpointIsNotReuploaded) {
    push("job-1");
    runtime_.polls_until_exit = 10;
    runtime_.on_start = [](const fs::path& dir) { write_text(dir / "checkpoint.dat", "same"); };
    runtime_.on_poll = [](const fs::path& dir, int poll) {
        if (poll == 9) {
            write_text(dir / "result.dat", "done");
        }
    };
    const auto stats = run();
    EXPECT_EQ(stats.checkpoints_uploaded, 1);
}

TEST_F(JobRunnerTest, CrashRequeuesWithIncrementedAttemptAndKeepsProgress) {
    push("job-1");
    runtime_.exit_code = 139;  // segfault
    runtime_.on_start = [](const fs::path& dir) { write_text(dir / "checkpoint.dat", "partial"); };

    const auto stats = run(1);

    EXPECT_EQ(stats.failed, 1);
    EXPECT_EQ(stored(checkpoint_key("job-1")), "partial");
    LocalQueue queue(dir_.str("queue"));
    EXPECT_EQ(queue.receive(std::chrono::seconds(5))->attempts, 1);
}

TEST_F(JobRunnerTest, DeadLettersAfterMaxAttempts) {
    push("job-1", /*attempts=*/2);
    runtime_.exit_code = 1;
    const auto stats = run();
    EXPECT_EQ(stats.dead_lettered, 1);
    EXPECT_TRUE(queue_dir_has("dead", "job-1"));
}

TEST_F(JobRunnerTest, StartFailureCountsAsAttempt) {
    push("job-1");
    runtime_.start_throws = true;
    const auto stats = run(1);
    EXPECT_EQ(stats.failed, 1);
    EXPECT_TRUE(queue_dir_has("pending", "job-1"));
}

TEST_F(JobRunnerTest, RequestStopBehavesLikeInterruption) {
    push("job-1");
    runtime_.polls_until_exit = 100000;
    JobRunner runner(config_, "test-manager", storage_, queue_, runtime_, watcher_, events_);
    std::thread stopper([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        runner.request_stop();
    });
    const auto stats = runner.run();
    stopper.join();
    EXPECT_EQ(stats.interrupted, 1);
    EXPECT_FALSE(stats.received_interruption);
    EXPECT_TRUE(queue_dir_has("pending", "job-1"));
}

TEST_F(JobRunnerTest, RebalanceDrainsAfterCurrentJob) {
    push("job-1");
    push("job-2");
    runtime_.polls_until_exit = 4;
    runtime_.on_start = [&](const fs::path& dir) {
        write_text(dir / "result.dat", "done");
        watcher_.rebalance = true;
    };
    const auto stats = run();
    EXPECT_EQ(stats.completed, 1);  // job-1 finished normally...
    EXPECT_EQ(runtime_.started.size(), 1U);  // ...and job-2 was left for another machine
    EXPECT_TRUE(queue_dir_has("pending", "job-2"));
}

TEST_F(JobRunnerTest, LostLeaseAbandonsJobWithoutUploading) {
    config_.timing.lease_duration = std::chrono::seconds(1);
    config_.timing.checkpoint_sync_interval = std::chrono::seconds(10);  // no sync before expiry
    config_.timing.poll_interval = std::chrono::milliseconds(5);
    push("job-1");
    runtime_.polls_until_exit = 100000;

    // Simulate another manager stealing the job: the lease expires while a
    // competing consumer re-claims it, so our renewal must fail.
    std::thread thief([&] {
        LocalQueue other(dir_.str("queue"));
        while (true) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            const auto pending = dir_.path() / "queue" / "inflight" / "job-1.json";
            if (!fs::exists(pending)) {
                continue;
            }
            // Expire the lease immediately by rewriting it.
            auto doc = nlohmann::json::parse(read_file(pending));
            doc["lease_until_ms"] = 0;
            write_file_atomically(pending, doc.dump());
            if (other.receive(std::chrono::seconds(60))) {
                return;
            }
        }
    });

    const auto stats = run(1);
    thief.join();

    EXPECT_EQ(stats.lease_lost, 1);
    EXPECT_FALSE(storage_.exists(checkpoint_key("job-1")));
}

TEST_F(JobRunnerTest, EmitsStructuredEvents) {
    push("job-1");
    runtime_.on_start = [](const fs::path& dir) { write_text(dir / "result.dat", "done"); };
    run();
    const std::string log = read_file(dir_.str("events.jsonl"));
    for (const char* event : {"manager_started", "job_claimed", "job_started", "job_completed",
                              "manager_stopped"}) {
        EXPECT_NE(log.find(std::string("\"event\":\"") + event + "\""), std::string::npos) << event;
    }
}
