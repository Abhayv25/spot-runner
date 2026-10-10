// spot-runner manager entry point.
//
//   spot-runner run     --config FILE [--verbose]
//   spot-runner enqueue --config FILE JOB.json [JOB.json ...]
//   spot-runner check   --config FILE

#include <unistd.h>

#include <csignal>
#include <cstring>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "spot_runner/config.h"
#include "spot_runner/docker_client.h"
#include "spot_runner/errors.h"
#include "spot_runner/event_log.h"
#include "spot_runner/imds_watcher.h"
#include "spot_runner/job_runner.h"
#include "spot_runner/local_queue.h"
#include "spot_runner/local_storage.h"
#include "spot_runner/logger.h"
#include "spot_runner/process_runtime.h"
#include "spot_runner/util.h"

#ifdef SPOT_RUNNER_WITH_AWS
#include <aws/core/Aws.h>

#include "spot_runner/s3_storage.h"
#include "spot_runner/sqs_queue.h"
#endif

namespace sr = spot_runner;

namespace {

constexpr int kExitOk = 0;
constexpr int kExitFailure = 1;
constexpr int kExitUsage = 2;

struct Options {
    std::string command;
    std::string config_path;
    std::vector<std::string> positional;
    bool verbose = false;
};

void print_usage() {
    std::cerr << "usage:\n"
                 "  spot-runner run     --config FILE [--verbose]\n"
                 "  spot-runner enqueue --config FILE JOB.json [JOB.json ...]\n"
                 "  spot-runner check   --config FILE\n";
}

std::optional<Options> parse_args(int argc, char* argv[]) {
    if (argc < 2) {
        return std::nullopt;
    }
    Options options;
    options.command = argv[1];
    for (int i = 2; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--config" && i + 1 < argc) {
            options.config_path = argv[++i];
        } else if (arg == "--verbose" || arg == "-v") {
            options.verbose = true;
        } else if (arg == "--help" || arg == "-h") {
            return std::nullopt;
        } else if (!arg.empty() && arg[0] == '-') {
            std::cerr << "unknown option " << arg << '\n';
            return std::nullopt;
        } else {
            options.positional.push_back(arg);
        }
    }
    if (options.config_path.empty()) {
        std::cerr << "--config is required\n";
        return std::nullopt;
    }
    return options;
}

std::string resolve_manager_id(const sr::Config& config) {
    if (config.manager_id != "auto") {
        return config.manager_id;
    }
    if (config.backend == sr::Backend::Aws) {
        if (auto instance_id = sr::fetch_instance_id()) {
            return *instance_id;
        }
    }
    char host[256] = {};
    ::gethostname(host, sizeof(host) - 1);
    return std::string(host) + "-" + std::to_string(::getpid());
}

// The concrete backends for one configuration. Owning them here keeps
// construction (the only place that knows about AWS vs local) out of JobRunner.
struct Backends {
    std::unique_ptr<sr::Storage> storage;
    std::unique_ptr<sr::JobQueue> queue;
    std::unique_ptr<sr::InterruptionWatcher> watcher;
    std::unique_ptr<sr::ContainerRuntime> runtime;
};

Backends make_backends(const sr::Config& config) {
    Backends b;
    if (config.backend == sr::Backend::Local) {
        b.storage = std::make_unique<sr::LocalStorage>(config.local.storage_dir);
        b.queue = std::make_unique<sr::LocalQueue>(config.local.queue_dir);
        b.watcher = std::make_unique<sr::FileWatcher>(config.local.interrupt_file);
    } else {
#ifdef SPOT_RUNNER_WITH_AWS
        b.storage = std::make_unique<sr::S3Storage>(config.aws.region, config.aws.bucket,
                                                    config.aws.key_prefix);
        b.queue = std::make_unique<sr::SqsQueue>(config.aws.region, config.aws.queue_url,
                                                 config.aws.dead_letter_queue_url);
        b.watcher = std::make_unique<sr::ImdsWatcher>();
#else
        throw sr::ConfigError(
            "backend \"aws\" requires a build with -DSPOT_RUNNER_ENABLE_AWS=ON");
#endif
    }

    if (config.runtime == sr::RuntimeKind::Docker) {
        b.runtime = std::make_unique<sr::DockerClient>(config.docker.socket_path,
                                                       config.docker.registry_auth_file);
    } else {
        b.runtime = std::make_unique<sr::ProcessRuntime>(config.work_dir + "/logs");
    }
    return b;
}

// Signal handling uses a dedicated thread and sigwait() instead of an
// asynchronous handler: SIGINT and SIGTERM are blocked in every thread, and
// this thread receives them synchronously, so it can safely log and call into
// JobRunner. The first signal starts a graceful shutdown; a second one exits
// immediately.
class SignalThread {
public:
    explicit SignalThread(sr::JobRunner& runner) : runner_(runner) {
        thread_ = std::thread([this] { loop(); });
    }
    ~SignalThread() {
        // SIGUSR1 is part of the waited set and only means "exit the thread".
        pthread_kill(thread_.native_handle(), SIGUSR1);
        thread_.join();
    }
    SignalThread(const SignalThread&) = delete;
    SignalThread& operator=(const SignalThread&) = delete;

    static void block_signals() {
        sigset_t set = signal_set();
        pthread_sigmask(SIG_BLOCK, &set, nullptr);
    }

private:
    static sigset_t signal_set() {
        sigset_t set;
        sigemptyset(&set);
        sigaddset(&set, SIGINT);
        sigaddset(&set, SIGTERM);
        sigaddset(&set, SIGUSR1);
        return set;
    }

    void loop() {
        const sigset_t set = signal_set();
        int received = 0;
        while (true) {
            int sig = 0;
            if (sigwait(&set, &sig) != 0) {
                continue;
            }
            if (sig == SIGUSR1) {
                return;
            }
            if (++received == 1) {
                sr::log::warn(std::string("received ") + strsignal(sig) +
                              ", checkpointing current job and shutting down (repeat to force)");
                runner_.request_stop();
            } else {
                sr::log::error("second signal, exiting immediately");
                std::_Exit(130);
            }
        }
    }

    sr::JobRunner& runner_;
    std::thread thread_;
};

int command_run(const sr::Config& config) {
    const std::string manager_id = resolve_manager_id(config);
    sr::log::set_component(manager_id);

    Backends backends = make_backends(config);
    if (!backends.runtime->ping()) {
        sr::log::error("container runtime is not reachable (is Docker running?)");
        return kExitFailure;
    }

    sr::EventLog events(config.event_log, manager_id);
    sr::JobRunner runner(config, manager_id, *backends.storage, *backends.queue,
                         *backends.runtime, *backends.watcher, events);

    sr::RunnerStats stats;
    {
        SignalThread signals(runner);
        stats = runner.run();
    }

    sr::log::info("summary: completed=" + std::to_string(stats.completed) +
                  " interrupted=" + std::to_string(stats.interrupted) +
                  " failed=" + std::to_string(stats.failed) +
                  " dead_lettered=" + std::to_string(stats.dead_lettered) +
                  " lease_lost=" + std::to_string(stats.lease_lost) +
                  " checkpoints_uploaded=" + std::to_string(stats.checkpoints_uploaded));
    return kExitOk;
}

int command_enqueue(const sr::Config& config, const std::vector<std::string>& files) {
    if (files.empty()) {
        std::cerr << "enqueue: no job files given\n";
        return kExitUsage;
    }
    Backends backends = make_backends(config);
    int failures = 0;
    for (const auto& file : files) {
        try {
            const sr::Job job = sr::job_from_json(sr::read_file(file));
            backends.queue->push(job);
            std::cout << "queued " << job.job_id << '\n';
        } catch (const std::exception& e) {
            std::cerr << file << ": " << e.what() << '\n';
            ++failures;
        }
    }
    return failures == 0 ? kExitOk : kExitFailure;
}

int command_check(const sr::Config& config) {
    Backends backends = make_backends(config);
    const bool runtime_ok = backends.runtime->ping();
    std::cout << "container runtime: " << (runtime_ok ? "ok" : "UNREACHABLE") << '\n';
    std::cout << "manager id:        " << resolve_manager_id(config) << '\n';
    const auto notice = backends.watcher->check();
    std::cout << "interruption:      " << (notice ? "NOTICE PRESENT" : "none") << '\n';
    return runtime_ok ? kExitOk : kExitFailure;
}

int dispatch(const Options& options) {
    const sr::Config config = sr::load_config(options.config_path);
    if (options.command == "run") {
        return command_run(config);
    }
    if (options.command == "enqueue") {
        return command_enqueue(config, options.positional);
    }
    if (options.command == "check") {
        return command_check(config);
    }
    std::cerr << "unknown command '" << options.command << "'\n";
    print_usage();
    return kExitUsage;
}

}  // namespace

int main(int argc, char* argv[]) {
    // Writing to a socket whose peer has closed must surface as EPIPE, not kill us.
    std::signal(SIGPIPE, SIG_IGN);
    // Block before any thread exists so every thread inherits the mask.
    SignalThread::block_signals();

    const auto options = parse_args(argc, argv);
    if (!options) {
        print_usage();
        return kExitUsage;
    }
    if (options->verbose) {
        sr::log::set_level(sr::LogLevel::Debug);
    }

#ifdef SPOT_RUNNER_WITH_AWS
    Aws::SDKOptions aws_options;
    Aws::InitAPI(aws_options);
#endif

    int exit_code = kExitFailure;
    try {
        exit_code = dispatch(*options);
    } catch (const sr::ConfigError& e) {
        std::cerr << "config error: " << e.what() << '\n';
        exit_code = kExitUsage;
    } catch (const std::exception& e) {
        sr::log::error(std::string("fatal: ") + e.what());
        exit_code = kExitFailure;
    }

#ifdef SPOT_RUNNER_WITH_AWS
    // All SDK clients were destroyed when dispatch() returned.
    Aws::ShutdownAPI(aws_options);
#endif
    return exit_code;
}
