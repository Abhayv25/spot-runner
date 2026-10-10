#include <gtest/gtest.h>

#include <mutex>
#include <set>
#include <thread>

#include "spot_runner/errors.h"
#include "spot_runner/job_queue.h"
#include "spot_runner/local_queue.h"
#include "temp_dir.h"

using namespace spot_runner;
using spot_runner::testing::TempDir;
using std::chrono::seconds;

namespace {

Job make_job(const std::string& id) {
    Job job;
    job.job_id = id;
    job.image = "prime-counter:latest";
    job.env = {{"TARGET", "1000"}};
    return job;
}

}  // namespace

TEST(JobJson, RoundTrips) {
    Job job = make_job("job-1");
    job.attempts = 2;
    const Job copy = job_from_json(job_to_json(job));
    EXPECT_EQ(copy.job_id, "job-1");
    EXPECT_EQ(copy.image, "prime-counter:latest");
    EXPECT_EQ(copy.env.at("TARGET"), "1000");
    EXPECT_EQ(copy.attempts, 2);
}

TEST(JobJson, ConvertsNumericEnvValuesToStrings) {
    const Job job = job_from_json(R"({"job_id":"a","image":"i","env":{"TARGET":500}})");
    EXPECT_EQ(job.env.at("TARGET"), "500");
}

TEST(JobJson, RejectsUnsafeOrMissingFields) {
    EXPECT_THROW(job_from_json("not json"), std::invalid_argument);
    EXPECT_THROW(job_from_json(R"({"image":"i"})"), std::invalid_argument);
    EXPECT_THROW(job_from_json(R"({"job_id":"../etc","image":"i"})"), std::invalid_argument);
    EXPECT_THROW(job_from_json(R"({"job_id":"a/b","image":"i"})"), std::invalid_argument);
    EXPECT_THROW(job_from_json(R"({"job_id":"ok","image":""})"), std::invalid_argument);
}

class LocalQueueTest : public ::testing::Test {
protected:
    TempDir dir_;
    LocalQueue queue_{dir_.str("queue")};
};

TEST_F(LocalQueueTest, EmptyQueueReturnsNothing) {
    EXPECT_FALSE(queue_.receive(seconds(30)).has_value());
}

TEST_F(LocalQueueTest, ReceivedJobIsHiddenFromOtherConsumers) {
    queue_.push(make_job("job-1"));
    const auto first = queue_.receive(seconds(30));
    ASSERT_TRUE(first.has_value());
    EXPECT_EQ(first->job_id, "job-1");
    EXPECT_FALSE(first->receipt.empty());
    EXPECT_FALSE(queue_.receive(seconds(30)).has_value());
}

TEST_F(LocalQueueTest, ReceivesInFifoOrder) {
    queue_.push(make_job("job-a"));
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    queue_.push(make_job("job-b"));
    EXPECT_EQ(queue_.receive(seconds(30))->job_id, "job-a");
    EXPECT_EQ(queue_.receive(seconds(30))->job_id, "job-b");
}

TEST_F(LocalQueueTest, InterruptedJobKeepsItsPlaceInLine) {
    queue_.push(make_job("job-a"));
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    queue_.push(make_job("job-b"));
    queue_.release(*queue_.receive(seconds(30)), /*count_failure=*/false);
    EXPECT_EQ(queue_.receive(seconds(30))->job_id, "job-a");
}

TEST_F(LocalQueueTest, FailedJobGoesToTheBackOfTheLine) {
    queue_.push(make_job("job-a"));
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    queue_.push(make_job("job-b"));
    queue_.release(*queue_.receive(seconds(30)), /*count_failure=*/true);
    EXPECT_EQ(queue_.receive(seconds(30))->job_id, "job-b");
}

TEST_F(LocalQueueTest, RejectsDuplicateJobIds) {
    queue_.push(make_job("job-1"));
    EXPECT_THROW(queue_.push(make_job("job-1")), std::invalid_argument);
}

TEST_F(LocalQueueTest, CompleteRemovesJobPermanently) {
    queue_.push(make_job("job-1"));
    const auto job = queue_.receive(seconds(1));
    queue_.complete(*job);
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    EXPECT_FALSE(queue_.receive(seconds(1)).has_value());
}

TEST_F(LocalQueueTest, ReleaseMakesJobVisibleWithoutCountingFailure) {
    queue_.push(make_job("job-1"));
    queue_.release(*queue_.receive(seconds(30)), false);
    const auto again = queue_.receive(seconds(30));
    ASSERT_TRUE(again.has_value());
    EXPECT_EQ(again->attempts, 0);
}

TEST_F(LocalQueueTest, ReleaseAsFailureIncrementsAttempts) {
    queue_.push(make_job("job-1"));
    queue_.release(*queue_.receive(seconds(30)), true);
    EXPECT_EQ(queue_.receive(seconds(30))->attempts, 1);
}

TEST_F(LocalQueueTest, ExpiredLeaseReturnsJobToQueue) {
    // The "machine vanished" case: nobody completes or releases the job.
    queue_.push(make_job("job-1"));
    ASSERT_TRUE(queue_.receive(seconds(1)).has_value());
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    const auto again = queue_.receive(seconds(30));
    ASSERT_TRUE(again.has_value());
    EXPECT_EQ(again->job_id, "job-1");
}

TEST_F(LocalQueueTest, ExtendLeaseKeepsJobHidden) {
    queue_.push(make_job("job-1"));
    const auto job = queue_.receive(seconds(1));
    queue_.extend_lease(*job, seconds(10));
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    EXPECT_FALSE(queue_.receive(seconds(1)).has_value());
}

TEST_F(LocalQueueTest, StaleReceiptIsRejectedAfterReclaim) {
    queue_.push(make_job("job-1"));
    const auto old_claim = queue_.receive(seconds(1));
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    const auto new_claim = queue_.receive(seconds(30));
    ASSERT_TRUE(new_claim.has_value());

    // The first consumer must not be able to touch the job any more.
    EXPECT_THROW(queue_.extend_lease(*old_claim, seconds(30)), LeaseLostError);
    EXPECT_THROW(queue_.complete(*old_claim), LeaseLostError);
    EXPECT_THROW(queue_.release(*old_claim, false), LeaseLostError);
    // The new owner is unaffected.
    EXPECT_NO_THROW(queue_.complete(*new_claim));
}

TEST_F(LocalQueueTest, DeadLetterRemovesJobFromCirculation) {
    queue_.push(make_job("job-1"));
    queue_.dead_letter(*queue_.receive(seconds(30)));
    EXPECT_FALSE(queue_.receive(seconds(30)).has_value());
    EXPECT_TRUE(std::filesystem::exists(dir_.path() / "queue" / "dead" / "job-1.json"));
}

TEST_F(LocalQueueTest, ConcurrentConsumersNeverClaimTheSameJob) {
    constexpr int kJobs = 200;
    constexpr int kConsumers = 8;
    for (int i = 0; i < kJobs; ++i) {
        queue_.push(make_job("job-" + std::to_string(i)));
    }

    std::mutex mutex;
    std::multiset<std::string> claimed;
    std::vector<std::thread> consumers;
    for (int c = 0; c < kConsumers; ++c) {
        // Each consumer opens its own LocalQueue, like separate manager processes.
        consumers.emplace_back([&] {
            LocalQueue queue(dir_.str("queue"));
            while (auto job = queue.receive(seconds(60))) {
                std::lock_guard<std::mutex> lock(mutex);
                claimed.insert(job->job_id);
            }
        });
    }
    for (auto& t : consumers) {
        t.join();
    }

    EXPECT_EQ(claimed.size(), static_cast<std::size_t>(kJobs));
    for (int i = 0; i < kJobs; ++i) {
        EXPECT_EQ(claimed.count("job-" + std::to_string(i)), 1U);
    }
}
