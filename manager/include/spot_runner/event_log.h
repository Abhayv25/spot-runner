#pragma once

#include <fstream>
#include <mutex>
#include <string>

#include <nlohmann/json.hpp>

namespace spot_runner {

// Append-only structured event stream (JSON Lines). Each line records one
// state transition with a millisecond timestamp, e.g.
//   {"ts":1760114401123,"manager":"m-1","event":"checkpoint_uploaded","job":"job-001"}
// The chaos test reads these files to compute recovery and data-loss metrics,
// and in production they are the input for dashboards and alerts.
class EventLog {
public:
    // An empty path disables event logging.
    explicit EventLog(const std::string& path, std::string manager_id);

    void emit(const std::string& event, nlohmann::json fields = nlohmann::json::object());

private:
    std::mutex mutex_;
    std::ofstream out_;
    std::string manager_id_;
};

}  // namespace spot_runner
