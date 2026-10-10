#include "spot_runner/event_log.h"

#include <filesystem>
#include <stdexcept>

#include "spot_runner/util.h"

namespace spot_runner {

EventLog::EventLog(const std::string& path, std::string manager_id)
    : manager_id_(std::move(manager_id)) {
    if (path.empty()) {
        return;
    }
    const std::filesystem::path file(path);
    if (file.has_parent_path()) {
        std::filesystem::create_directories(file.parent_path());
    }
    out_.open(file, std::ios::app);
    if (!out_) {
        throw std::runtime_error("cannot open event log " + path);
    }
}

void EventLog::emit(const std::string& event, nlohmann::json fields) {
    if (!out_.is_open()) {
        return;
    }
    fields["ts"] = unix_millis();
    fields["manager"] = manager_id_;
    fields["event"] = event;
    std::lock_guard<std::mutex> lock(mutex_);
    out_ << fields.dump() << '\n';
    out_.flush();
}

}  // namespace spot_runner
