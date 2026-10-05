// =============================================================================
// docker_client.h: the REAL container runtime, talks to Docker (PHASE 5)
// =============================================================================
// No Docker SDK. You send HTTP requests to Docker's API over its Unix socket using
// your own http_client. Docs to keep open: search "Docker Engine API reference".
//
// STEP 1: #pragma once
//   includes "spot_runner/container_runtime.h" and "spot_runner/http_client.h"
//   namespace spot_runner
//
// STEP 2: class DockerClient : public ContainerRuntime
//   public:
//     explicit DockerClient(const std::string& socket_path);
//     Then one line per interface function, each ending in "override":
//       bool ping() override;  ... and so on for all 7 functions.
//   private:
//     HttpResponse call(const std::string& method, const std::string& path,
//                       const std::string& json_body = "");
//         (Helper: builds the request, sends it, throws if status >= 400.)
//     Endpoint endpoint_;
//     int timeout_ms_ = 30000;
// =============================================================================
