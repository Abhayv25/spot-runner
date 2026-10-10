#include <filesystem>
#include <fstream>
#include <string>

#include "spot_runner/interruption_watcher.h"

namespace spot_runner {

FileWatcher::FileWatcher(std::string trigger_path) : trigger_path_(std::move(trigger_path)) {}

std::optional<InterruptionNotice> FileWatcher::check() {
    std::error_code ec;
    if (!std::filesystem::exists(trigger_path_, ec) || ec) {
        return std::nullopt;
    }
    std::string first_word;
    std::ifstream(trigger_path_) >> first_word;
    InterruptionNotice notice;
    if (first_word == "rebalance") {
        notice.kind = InterruptionNotice::Kind::RebalanceRecommendation;
    } else {
        notice.action = "terminate";
    }
    notice.time = "now";
    return notice;
}

}  // namespace spot_runner
