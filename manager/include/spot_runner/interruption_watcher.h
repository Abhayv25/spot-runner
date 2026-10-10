#pragma once

#include <optional>
#include <string>

namespace spot_runner {

struct InterruptionNotice {
    enum class Kind {
        // The instance will be reclaimed in about two minutes: checkpoint now.
        Interruption,
        // EC2 rebalance recommendation: the instance is at elevated risk of
        // interruption. An early hint, arriving before the two-minute notice.
        RebalanceRecommendation,
    };

    Kind kind = Kind::Interruption;
    std::string action;  // "terminate", "stop", "hibernate" (interruptions only)
    std::string time;    // when the action happens or the notice was issued
};

// Answers "is this machine about to be taken away?". Implementations must not
// throw: a failed check is logged and reported as "no notice", because a
// transient metadata-service hiccup must never crash the manager.
class InterruptionWatcher {
public:
    virtual ~InterruptionWatcher() = default;
    virtual std::optional<InterruptionNotice> check() = 0;
};

// Development watcher: the interruption fires when a trigger file exists.
//   touch run/interrupt-m1      simulate the two-minute warning
//   echo rebalance > run/interrupt-m1   simulate a rebalance recommendation
class FileWatcher : public InterruptionWatcher {
public:
    explicit FileWatcher(std::string trigger_path);
    std::optional<InterruptionNotice> check() override;

private:
    std::string trigger_path_;
};

}  // namespace spot_runner
