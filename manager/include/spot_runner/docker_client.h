#pragma once

#include <chrono>
#include <string>

#include "spot_runner/container_runtime.h"
#include "spot_runner/http_client.h"

namespace spot_runner {

// ContainerRuntime backed by the Docker Engine HTTP API, spoken directly over
// the daemon's Unix socket with our own HTTP client (no Docker SDK).
// API reference: https://docs.docker.com/reference/api/engine/
class DockerClient : public ContainerRuntime {
public:
    static constexpr const char* kContainerCheckpointDir = "/checkpoint";

    explicit DockerClient(std::string socket_path, std::string registry_auth_file = "");

    bool ping() override;
    void ensure_image(const std::string& image) override;
    std::string start(const ContainerSpec& spec) override;
    ContainerStatus inspect(const std::string& handle) override;
    void stop(const std::string& handle, std::chrono::seconds grace) override;
    void remove(const std::string& handle) override;
    std::string logs_tail(const std::string& handle, int lines) override;
    void remove_orphans(const std::string& manager_id) override;

    // Exposed for tests: the JSON body sent to POST /containers/create.
    static std::string build_create_body(const ContainerSpec& spec, const std::string& user);

    // Exposed for tests: decodes Docker's multiplexed log stream framing.
    static std::string demultiplex_logs(const std::string& raw);

private:
    HttpResponse call(const std::string& method, const std::string& target,
                      const std::string& json_body = "",
                      std::chrono::milliseconds timeout = std::chrono::seconds(30));

    Endpoint endpoint_;
    std::string registry_auth_file_;
};

}  // namespace spot_runner
