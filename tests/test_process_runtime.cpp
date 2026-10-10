#include <gtest/gtest.h>

#include <thread>

#include "spot_runner/errors.h"
#include "spot_runner/process_runtime.h"
#include "spot_runner/util.h"
#include "temp_dir.h"

using namespace spot_runner;
using spot_runner::testing::TempDir;
namespace fs = std::filesystem;

namespace {

ContainerStatus wait_for_exit(ProcessRuntime& runtime, const std::string& handle) {
    for (int i = 0; i < 1000; ++i) {
        const auto status = runtime.inspect(handle);
        if (!status.running) {
            return status;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    ADD_FAILURE() << "process did not exit";
    return {};
}

ContainerSpec prime_counter(const TempDir& dir, const std::string& target) {
    ContainerSpec spec;
    spec.image = PRIME_COUNTER_PATH;
    spec.name = "test-job";
    spec.env = {{"JOB_ID", "test-job"}, {"TARGET", target}, {"AUTOSAVE_MS", "50"}};
    spec.checkpoint_dir = dir.str("job");
    fs::create_directories(spec.checkpoint_dir);
    return spec;
}

}  // namespace

TEST(ProcessRuntime, RunsJobToCompletion) {
    TempDir dir;
    ProcessRuntime runtime(dir.str("logs"));
    const auto spec = prime_counter(dir, "100000");

    const auto handle = runtime.start(spec);
    const auto status = wait_for_exit(runtime, handle);

    EXPECT_EQ(status.exit_code, 0);
    EXPECT_NE(read_file(fs::path(spec.checkpoint_dir) / "result.dat").find("primes_found=9592"),
              std::string::npos);
    EXPECT_NE(runtime.logs_tail(handle, 5).find("done: 9592 primes"), std::string::npos);
}

TEST(ProcessRuntime, StopDeliversSigtermAndJobExits75WithCheckpoint) {
    TempDir dir;
    ProcessRuntime runtime(dir.str("logs"));
    const auto spec = prime_counter(dir, "2000000000");

    const auto handle = runtime.start(spec);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    runtime.stop(handle, std::chrono::seconds(5));

    const auto status = runtime.inspect(handle);
    EXPECT_FALSE(status.running);
    EXPECT_EQ(status.exit_code, 75);
    EXPECT_TRUE(fs::exists(fs::path(spec.checkpoint_dir) / "checkpoint.dat"));
}

TEST(ProcessRuntime, MissingExecutableIsRejected) {
    TempDir dir;
    ProcessRuntime runtime(dir.str("logs"));
    EXPECT_THROW(runtime.ensure_image("/nonexistent/job"), RuntimeError);
}

TEST(ProcessRuntime, UnknownHandleReportsNotExisting) {
    TempDir dir;
    ProcessRuntime runtime(dir.str("logs"));
    EXPECT_FALSE(runtime.inspect("12345").exists);
    EXPECT_NO_THROW(runtime.remove("12345"));
}
