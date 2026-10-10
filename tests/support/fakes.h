#pragma once

#include <atomic>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "spot_runner/container_runtime.h"
#include "spot_runner/interruption_watcher.h"

namespace spot_runner::testing {

// In-memory ContainerRuntime. A test scripts the "job" with callbacks that
// write files into the shared checkpoint directory, the way a real job would.
class FakeRuntime : public ContainerRuntime {
public:
    // Called once when the job starts.
    std::function<void(const std::filesystem::path& dir)> on_start;
    // Called on every inspect() while running; `poll` counts from 1.
    std::function<void(const std::filesystem::path& dir, int poll)> on_poll;
    // Called when stop() is requested (the job's SIGTERM handler).
    std::function<void(const std::filesystem::path& dir)> on_stop;

    int polls_until_exit = 3;
    int exit_code = 0;
    int exit_code_when_stopped = 75;
    bool start_throws = false;

    // Observations for assertions.
    std::vector<ContainerSpec> started;
    int stop_calls = 0;
    int remove_calls = 0;
    bool checkpoint_present_at_start = false;

    bool ping() override { return true; }
    void ensure_image(const std::string&) override {}

    std::string start(const ContainerSpec& spec) override {
        if (start_throws) {
            throw std::runtime_error("image not found");
        }
        started.push_back(spec);
        dir_ = spec.checkpoint_dir;
        checkpoint_present_at_start = std::filesystem::exists(dir_ / "checkpoint.dat");
        polls_ = 0;
        stopped_ = false;
        running_ = true;
        if (on_start) {
            on_start(dir_);
        }
        return "fake-" + std::to_string(started.size());
    }

    ContainerStatus inspect(const std::string&) override {
        if (running_) {
            ++polls_;
            if (on_poll) {
                on_poll(dir_, polls_);
            }
            if (polls_ >= polls_until_exit) {
                running_ = false;
            }
        }
        return ContainerStatus{true, running_, stopped_ ? exit_code_when_stopped : exit_code};
    }

    void stop(const std::string&, std::chrono::seconds) override {
        ++stop_calls;
        if (running_) {
            if (on_stop) {
                on_stop(dir_);
            }
            running_ = false;
            stopped_ = true;
        }
    }

    void remove(const std::string&) override { ++remove_calls; }
    std::string logs_tail(const std::string&, int) override { return "fake job output"; }
    void remove_orphans(const std::string&) override {}

private:
    std::filesystem::path dir_;
    int polls_ = 0;
    bool running_ = false;
    bool stopped_ = false;
};

// InterruptionWatcher controlled by the test.
class FakeWatcher : public InterruptionWatcher {
public:
    std::atomic<bool> interrupt{false};
    std::atomic<bool> rebalance{false};
    int fire_interrupt_after_checks = -1;  // -1 = never fire automatically
    int checks = 0;

    std::optional<InterruptionNotice> check() override {
        ++checks;
        if (fire_interrupt_after_checks >= 0 && checks > fire_interrupt_after_checks) {
            interrupt = true;
        }
        if (interrupt) {
            return InterruptionNotice{InterruptionNotice::Kind::Interruption, "terminate", "test"};
        }
        if (rebalance) {
            return InterruptionNotice{InterruptionNotice::Kind::RebalanceRecommendation, "", "test"};
        }
        return std::nullopt;
    }
};

inline void write_text(const std::filesystem::path& path, const std::string& text) {
    std::ofstream(path) << text;
}

}  // namespace spot_runner::testing
