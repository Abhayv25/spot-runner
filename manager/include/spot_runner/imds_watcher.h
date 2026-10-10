#pragma once

#include <chrono>
#include <optional>
#include <string>

#include "spot_runner/http_client.h"
#include "spot_runner/interruption_watcher.h"

namespace spot_runner {

// Client for the EC2 Instance Metadata Service (IMDSv2), reachable only from
// inside an EC2 instance at http://169.254.169.254.
//
// IMDSv2 is session-based: PUT /latest/api/token returns a token that must be
// sent as X-aws-ec2-metadata-token on every GET. The token is cached until
// shortly before it expires and refreshed on a 401.
class ImdsClient {
public:
    ImdsClient();
    explicit ImdsClient(Endpoint endpoint);

    // GET a metadata path. Returns nullopt on 404, the body on 200; throws
    // HttpError on transport failures or other statuses.
    std::optional<std::string> get(const std::string& path);

private:
    std::string token();

    Endpoint endpoint_;
    std::string token_;
    std::chrono::steady_clock::time_point token_expiry_{};
};

// Polls IMDS for spot interruption notices and rebalance recommendations:
//   /latest/meta-data/spot/instance-action               404 until a notice exists
//   /latest/meta-data/events/recommendations/rebalance   404 until a recommendation
class ImdsWatcher : public InterruptionWatcher {
public:
    ImdsWatcher() = default;
    explicit ImdsWatcher(Endpoint endpoint);

    std::optional<InterruptionNotice> check() override;

private:
    ImdsClient client_;
};

// Returns this instance's ID (for example "i-0abc123..."), or nullopt when not
// on EC2. Fails fast: each IMDS request has a one-second timeout.
std::optional<std::string> fetch_instance_id();

}  // namespace spot_runner
