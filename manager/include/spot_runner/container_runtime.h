#pragma once

#include <chrono>
#include <map>
#include <string>

namespace spot_runner {

struct ContainerSpec {
    std::string image;  // Docker image reference, or an executable path for ProcessRuntime
    std::string name;   // unique per running job, e.g. "spot-runner-job-001"
    std::map<std::string, std::string> env;
    std::map<std::string, std::string> labels;
    // Absolute host directory shared with the job. The runtime exposes it to
    // the job and sets CHECKPOINT_DIR to the path the job should use.
    std::string checkpoint_dir;
};

struct ContainerStatus {
    bool exists = false;
    bool running = false;
    int exit_code = 0;  // valid when exists && !running; 128+N if killed by signal N
};

// Something that can run a job and report how it exited. JobRunner depends
// only on this interface, so the same scheduling logic runs against Docker in
// production, against plain child processes in local chaos tests, and against
// an in-memory fake in unit tests.
class ContainerRuntime {
public:
    virtual ~ContainerRuntime() = default;

    // Returns true if the runtime is reachable and usable.
    virtual bool ping() = 0;

    // Makes sure the image is available locally (pulls if necessary).
    virtual void ensure_image(const std::string& image) = 0;

    // Starts the job; returns an opaque handle (container ID or PID).
    virtual std::string start(const ContainerSpec& spec) = 0;

    virtual ContainerStatus inspect(const std::string& handle) = 0;

    // Sends SIGTERM, waits up to `grace`, then SIGKILL. Blocks until stopped.
    virtual void stop(const std::string& handle, std::chrono::seconds grace) = 0;

    // Releases resources. Safe to call on a handle that no longer exists.
    virtual void remove(const std::string& handle) = 0;

    // Last lines of the job's stdout/stderr, for diagnostics.
    virtual std::string logs_tail(const std::string& handle, int lines) = 0;

    // Removes leftovers from a previous run of this manager (for example after
    // a crash). Matches on the "spot-runner.manager" label.
    virtual void remove_orphans(const std::string& manager_id) = 0;
};

}  // namespace spot_runner
