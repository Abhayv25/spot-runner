#include "spot_runner/config.h"

#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

#include "spot_runner/errors.h"

namespace spot_runner {

namespace {

using nlohmann::json;

// EC2 gives two minutes between the interruption notice and termination. The
// grace period plus the final upload and queue release must fit inside it.
constexpr std::chrono::seconds kSpotNoticeWindow{120};
constexpr std::chrono::seconds kShutdownReserve{20};

template <typename T>
T get_or(const json& object, const char* key, T fallback) {
    if (!object.contains(key)) {
        return fallback;
    }
    try {
        return object.at(key).get<T>();
    } catch (const json::exception& e) {
        throw ConfigError(std::string("invalid value for '") + key + "': " + e.what());
    }
}

std::string require_string(const json& object, const char* section, const char* key) {
    const auto value = get_or<std::string>(object, key, "");
    if (value.empty()) {
        throw ConfigError(std::string(section) + "." + key + " is required");
    }
    return value;
}

}  // namespace

Config parse_config(const std::string& json_text) {
    json root;
    try {
        root = json::parse(json_text);
    } catch (const json::parse_error& e) {
        throw ConfigError(std::string("config is not valid JSON: ") + e.what());
    }
    if (!root.is_object()) {
        throw ConfigError("config must be a JSON object");
    }

    Config config;

    const auto backend = get_or<std::string>(root, "backend", "local");
    if (backend == "local") {
        config.backend = Backend::Local;
    } else if (backend == "aws") {
        config.backend = Backend::Aws;
    } else {
        throw ConfigError("backend must be \"local\" or \"aws\", got \"" + backend + "\"");
    }

    const auto runtime = get_or<std::string>(root, "runtime", "docker");
    if (runtime == "docker") {
        config.runtime = RuntimeKind::Docker;
    } else if (runtime == "process") {
        config.runtime = RuntimeKind::Process;
    } else {
        throw ConfigError("runtime must be \"docker\" or \"process\", got \"" + runtime + "\"");
    }

    config.manager_id = get_or<std::string>(root, "manager_id", "auto");
    config.work_dir = get_or<std::string>(root, "work_dir", "");
    config.event_log = get_or<std::string>(root, "event_log", "");
    config.max_attempts = get_or<int>(root, "max_attempts", config.max_attempts);
    config.exit_when_idle = get_or<bool>(root, "exit_when_idle", config.exit_when_idle);
    config.drain_on_rebalance = get_or<bool>(root, "drain_on_rebalance", config.drain_on_rebalance);

    if (root.contains("timing")) {
        const json& t = root.at("timing");
        auto& timing = config.timing;
        timing.poll_interval = std::chrono::milliseconds(
            get_or<long long>(t, "poll_interval_ms", timing.poll_interval.count()));
        timing.checkpoint_sync_interval = std::chrono::milliseconds(get_or<long long>(
            t, "checkpoint_sync_interval_ms", timing.checkpoint_sync_interval.count()));
        timing.lease_duration = std::chrono::seconds(
            get_or<long long>(t, "lease_duration_s", timing.lease_duration.count()));
        timing.stop_grace_period = std::chrono::seconds(
            get_or<long long>(t, "stop_grace_period_s", timing.stop_grace_period.count()));
        timing.idle_backoff = std::chrono::milliseconds(
            get_or<long long>(t, "idle_backoff_ms", timing.idle_backoff.count()));
    }

    if (root.contains("docker")) {
        const json& d = root.at("docker");
        config.docker.socket_path = get_or<std::string>(d, "socket", config.docker.socket_path);
        config.docker.registry_auth_file = get_or<std::string>(d, "registry_auth_file", "");
    }

    if (config.backend == Backend::Local) {
        if (!root.contains("local")) {
            throw ConfigError("backend \"local\" requires a \"local\" section");
        }
        const json& l = root.at("local");
        config.local.storage_dir = require_string(l, "local", "storage_dir");
        config.local.queue_dir = require_string(l, "local", "queue_dir");
        config.local.interrupt_file = require_string(l, "local", "interrupt_file");
    } else {
        if (!root.contains("aws")) {
            throw ConfigError("backend \"aws\" requires an \"aws\" section");
        }
        const json& a = root.at("aws");
        config.aws.region = require_string(a, "aws", "region");
        config.aws.bucket = require_string(a, "aws", "bucket");
        config.aws.queue_url = require_string(a, "aws", "queue_url");
        config.aws.dead_letter_queue_url = require_string(a, "aws", "dead_letter_queue_url");
        config.aws.key_prefix = get_or<std::string>(a, "key_prefix", config.aws.key_prefix);
    }

    validate_config(config);
    return config;
}

Config load_config(const std::string& path) {
    std::ifstream in(path);
    if (!in) {
        throw ConfigError("cannot open config file " + path);
    }
    std::ostringstream text;
    text << in.rdbuf();
    return parse_config(text.str());
}

void validate_config(const Config& config) {
    if (config.manager_id.empty()) {
        throw ConfigError("manager_id must not be empty");
    }
    if (config.work_dir.empty()) {
        throw ConfigError("work_dir is required");
    }
    if (config.max_attempts < 1) {
        throw ConfigError("max_attempts must be at least 1");
    }

    const auto& t = config.timing;
    if (t.poll_interval.count() <= 0 || t.checkpoint_sync_interval.count() <= 0 ||
        t.lease_duration.count() <= 0 || t.stop_grace_period.count() <= 0 ||
        t.idle_backoff.count() <= 0) {
        throw ConfigError("all timing values must be positive");
    }
    if (t.stop_grace_period + kShutdownReserve > kSpotNoticeWindow) {
        throw ConfigError("timing.stop_grace_period_s must be at most " +
                          std::to_string((kSpotNoticeWindow - kShutdownReserve).count()) +
                          " so the final checkpoint upload fits in the 120 s spot notice window");
    }
    // Leases are renewed every lease/3 from the watch loop; the loop must run
    // several times per renewal period or a slow tick could let the lease lapse.
    if (std::chrono::duration_cast<std::chrono::milliseconds>(t.lease_duration) <
        t.poll_interval * 6) {
        throw ConfigError("timing.lease_duration_s must be at least 6x poll_interval_ms");
    }
}

}  // namespace spot_runner
