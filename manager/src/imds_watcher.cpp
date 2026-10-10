#include "spot_runner/imds_watcher.h"

#include <nlohmann/json.hpp>

#include "spot_runner/errors.h"
#include "spot_runner/logger.h"

namespace spot_runner {

namespace {

using nlohmann::json;

constexpr const char* kImdsHost = "169.254.169.254";
constexpr int kTokenTtlSeconds = 21600;  // 6 hours, the IMDSv2 maximum
constexpr auto kRequestTimeout = std::chrono::milliseconds(1000);
constexpr auto kTokenRefreshMargin = std::chrono::minutes(5);

}  // namespace

ImdsClient::ImdsClient() : endpoint_(Endpoint::tcp(kImdsHost, 80)) {}

ImdsClient::ImdsClient(Endpoint endpoint) : endpoint_(std::move(endpoint)) {}

std::string ImdsClient::token() {
    const auto now = std::chrono::steady_clock::now();
    if (!token_.empty() && now < token_expiry_) {
        return token_;
    }
    HttpRequest request;
    request.method = "PUT";
    request.target = "/latest/api/token";
    request.headers["X-aws-ec2-metadata-token-ttl-seconds"] = std::to_string(kTokenTtlSeconds);
    const auto response = send_request(endpoint_, request, kRequestTimeout);
    if (response.status != 200 || response.body.empty()) {
        throw HttpError("IMDS token request returned " + std::to_string(response.status));
    }
    token_ = response.body;
    token_expiry_ = now + std::chrono::seconds(kTokenTtlSeconds) - kTokenRefreshMargin;
    return token_;
}

std::optional<std::string> ImdsClient::get(const std::string& path) {
    for (int attempt = 0; attempt < 2; ++attempt) {
        HttpRequest request;
        request.target = path;
        request.headers["X-aws-ec2-metadata-token"] = token();
        const auto response = send_request(endpoint_, request, kRequestTimeout);
        if (response.status == 200) {
            return response.body;
        }
        if (response.status == 404) {
            return std::nullopt;
        }
        if (response.status == 401) {
            token_.clear();  // token expired or revoked; fetch a new one and retry once
            continue;
        }
        throw HttpError("IMDS GET " + path + " returned " + std::to_string(response.status));
    }
    throw HttpError("IMDS GET " + path + " unauthorized after token refresh");
}

ImdsWatcher::ImdsWatcher(Endpoint endpoint) : client_(std::move(endpoint)) {}

std::optional<InterruptionNotice> ImdsWatcher::check() {
    try {
        if (const auto body = client_.get("/latest/meta-data/spot/instance-action")) {
            const auto doc = json::parse(*body);
            InterruptionNotice notice;
            notice.kind = InterruptionNotice::Kind::Interruption;
            notice.action = doc.value("action", "terminate");
            notice.time = doc.value("time", "");
            return notice;
        }
        if (const auto body = client_.get("/latest/meta-data/events/recommendations/rebalance")) {
            const auto doc = json::parse(*body);
            InterruptionNotice notice;
            notice.kind = InterruptionNotice::Kind::RebalanceRecommendation;
            notice.time = doc.value("noticeTime", "");
            return notice;
        }
    } catch (const std::exception& e) {
        log::warn(std::string("IMDS check failed: ") + e.what());
    }
    return std::nullopt;
}

std::optional<std::string> fetch_instance_id() {
    try {
        ImdsClient client;
        return client.get("/latest/meta-data/instance-id");
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

}  // namespace spot_runner
