// End-to-end: the real prime-counter binary, run by real JobRunners through
// ProcessRuntime, LocalQueue, and LocalStorage. Proves the whole checkpoint
// contract: interrupt a job mid-run on one "machine", resume it on another,
// and get exactly the same answer as an uninterrupted run.

#include <gtest/gtest.h>

#include <thread>

#include "spot_runner/event_log.h"
#include "spot_runner/interruption_watcher.h"
#include "spot_runner/job_runner.h"
#include "spot_runner/local_queue.h"
#include "spot_runner/local_storage.h"
#include "spot_runner/process_runtime.h"
#include "spot_runner/util.h"
#include "temp_dir.h"

using namespace spot_runner;
using spot_runner::testing::TempDir;
namespace fs = std::filesystem;

namespace {

// Number of primes <= 3,000,000.
constexpr const char* kTarget = "3000000";
constexpr const char* kExpectedPrimes = "primes_found=216816";

struct Machine {
    Machine(const TempDir& shared, const std::string& id)
        : config(make_config(shared, id)),
          storage(shared.str("storage")),
          queue(shared.str("queue")),
          runtime(config.work_dir + "/logs"),
          watcher(config.local.interrupt_file),
          events(shared.str("events-" + id + ".jsonl"), id),
          runner(config, id, storage, queue, runtime, watcher, events) {}

    static Config make_config(const TempDir& shared, const std::string& id) {
        Config c;
        c.manager_id = id;
        c.work_dir = shared.str("work-" + id);
        c.runtime = RuntimeKind::Process;
        c.exit_when_idle = true;
        c.local.storage_dir = shared.str("storage");
        c.local.queue_dir = shared.str("queue");
        c.local.interrupt_file = shared.str("interrupt-" + id);
        c.timing.poll_interval = std::chrono::milliseconds(20);
        c.timing.checkpoint_sync_interval = std::chrono::milliseconds(100);
        c.timing.lease_duration = std::chrono::seconds(10);
        c.timing.stop_grace_period = std::chrono::seconds(5);
        c.timing.idle_backoff = std::chrono::milliseconds(50);
        return c;
    }

    Config config;
    LocalStorage storage;
    LocalQueue queue;
    ProcessRuntime runtime;
    FileWatcher watcher;
    EventLog events;
    JobRunner runner;
};

Job prime_job(const std::string& id) {
    Job job;
    job.job_id = id;
    job.image = PRIME_COUNTER_PATH;
    job.env = {{"TARGET", kTarget}, {"AUTOSAVE_MS", "50"}};
    return job;
}

std::string fetch_result(Storage& storage, const TempDir& dir, const std::string& id) {
    const std::string out = dir.str("result-" + id);
    return storage.get_file(result_key(id), out) ? read_file(out) : "";
}

}  // namespace

TEST(EndToEnd, UninterruptedRunProducesCorrectResult) {
    TempDir shared;
    Machine a(shared, "a");
    a.queue.push(prime_job("job-1"));

    const auto stats = a.runner.run();

    EXPECT_EQ(stats.completed, 1);
    EXPECT_NE(fetch_result(a.storage, shared, "job-1").find(kExpectedPrimes), std::string::npos);
}

TEST(EndToEnd, InterruptedJobResumesOnAnotherMachineWithIdenticalResult) {
    TempDir shared;
    Machine a(shared, "a");
    a.queue.push(prime_job("job-1"));

    // Machine A gets the spot interruption notice partway through the job.
    std::thread interrupter([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(400));
        write_file_atomically(a.config.local.interrupt_file, "");
    });
    const auto stats_a = a.runner.run();
    interrupter.join();

    ASSERT_EQ(stats_a.interrupted, 1);
    ASSERT_TRUE(a.storage.exists(checkpoint_key("job-1")));
    EXPECT_EQ(fetch_result(a.storage, shared, "job-1"), "");

    // Machine B picks the job up from the queue and resumes from the checkpoint.
    Machine b(shared, "b");
    const auto stats_b = b.runner.run();

    EXPECT_EQ(stats_b.completed, 1);
    EXPECT_NE(fetch_result(b.storage, shared, "job-1").find(kExpectedPrimes), std::string::npos);

    const std::string events_b = read_file(shared.str("events-b.jsonl"));
    EXPECT_NE(events_b.find("\"resumed\":true"), std::string::npos);
}
