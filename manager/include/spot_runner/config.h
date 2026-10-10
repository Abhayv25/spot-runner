#pragma once

#include <chrono>
#include <string>

namespace spot_runner {

enum class Backend { Local, Aws };
enum class RuntimeKind { Docker, Process };

struct TimingSettings {
    // How often the watch loop checks the container and the interruption watcher.
    std::chrono::milliseconds poll_interval{500};
    // How often a changed checkpoint is copied from the work dir to storage.
    std::chrono::milliseconds checkpoint_sync_interval{5000};
    // How long a claimed job stays invisible to other managers without renewal.
    // Renewed every lease_duration / 3.
    std::chrono::seconds lease_duration{60};
    // SIGTERM-to-SIGKILL grace period given to a job on interruption.
    std::chrono::seconds stop_grace_period{30};
    // Sleep between empty queue polls.
    std::chrono::milliseconds idle_backoff{2000};
};

struct LocalSettings {
    std::string storage_dir;
    std::string queue_dir;
    std::string interrupt_file;
};

struct AwsSettings {
    std::string region;
    std::string bucket;
    std::string queue_url;
    std::string dead_letter_queue_url;
    std::string key_prefix = "spot-runner/";
};

struct DockerSettings {
    std::string socket_path = "/var/run/docker.sock";
    // Optional file with registry credentials JSON, sent as X-Registry-Auth on pulls.
    std::string registry_auth_file;
};

struct Config {
    Backend backend = Backend::Local;
    RuntimeKind runtime = RuntimeKind::Docker;

    // Stable identity for logs, leases, and container labels. "auto" resolves
    // to the EC2 instance ID on AWS and to hostname-pid locally.
    std::string manager_id = "auto";
    std::string work_dir;
    std::string event_log;

    int max_attempts = 3;
    // Exit once the queue is empty instead of polling forever (batch mode).
    bool exit_when_idle = false;
    // On an EC2 rebalance recommendation, finish the current job but take no new ones.
    bool drain_on_rebalance = true;

    TimingSettings timing;
    DockerSettings docker;
    LocalSettings local;
    AwsSettings aws;
};

// Parses and validates a JSON config file. Throws ConfigError with a message
// that names the offending field.
Config load_config(const std::string& path);

// Parses config JSON from a string; used by load_config and by tests.
Config parse_config(const std::string& json_text);

// Throws ConfigError if any setting is invalid or the timing budget does not
// fit inside the two-minute spot interruption window.
void validate_config(const Config& config);

}  // namespace spot_runner
