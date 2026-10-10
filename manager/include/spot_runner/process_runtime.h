#pragma once

#include <sys/types.h>

#include <chrono>
#include <map>
#include <string>

#include "spot_runner/container_runtime.h"

namespace spot_runner {

// ContainerRuntime that runs the job as a plain child process (fork + execve).
// `ContainerSpec::image` is the path to an executable, and the job sees the
// shared directory at its real host path instead of /checkpoint.
//
// Used for local development and the chaos test on machines without Docker.
// It exercises exactly the same JobRunner code paths as DockerClient.
class ProcessRuntime : public ContainerRuntime {
public:
    // Job stdout/stderr go to <log_dir>/<name>.log.
    explicit ProcessRuntime(std::string log_dir);
    ~ProcessRuntime() override;

    ProcessRuntime(const ProcessRuntime&) = delete;
    ProcessRuntime& operator=(const ProcessRuntime&) = delete;

    bool ping() override { return true; }
    void ensure_image(const std::string& image) override;
    std::string start(const ContainerSpec& spec) override;
    ContainerStatus inspect(const std::string& handle) override;
    void stop(const std::string& handle, std::chrono::seconds grace) override;
    void remove(const std::string& handle) override;
    std::string logs_tail(const std::string& handle, int lines) override;
    void remove_orphans(const std::string& /*manager_id*/) override {}

private:
    struct Child {
        pid_t pid = -1;
        std::string log_path;
        bool exited = false;
        int exit_code = 0;
    };

    // Non-blocking reap; updates `child` if it has exited.
    void poll_exit(Child& child);
    Child& find(const std::string& handle);

    std::string log_dir_;
    std::map<std::string, Child> children_;
};

}  // namespace spot_runner
