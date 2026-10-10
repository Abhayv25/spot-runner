#include "spot_runner/local_queue.h"

#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <system_error>

#include <nlohmann/json.hpp>

#include "spot_runner/errors.h"
#include "spot_runner/util.h"

namespace spot_runner {

namespace fs = std::filesystem;
using nlohmann::json;

// RAII exclusive advisory lock on the queue's lock file. flock() locks belong to
// the open file description, so separate opens (separate processes, or
// separate threads in one process) exclude each other.
class LocalQueue::Lock {
public:
    explicit Lock(const fs::path& path) {
        fd_ = ::open(path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0644);
        if (fd_ < 0) {
            throw std::system_error(errno, std::generic_category(), "open " + path.string());
        }
        while (::flock(fd_, LOCK_EX) != 0) {
            if (errno != EINTR) {
                const int saved = errno;
                ::close(fd_);
                throw std::system_error(saved, std::generic_category(), "flock " + path.string());
            }
        }
    }
    ~Lock() {
        ::flock(fd_, LOCK_UN);
        ::close(fd_);
    }
    Lock(const Lock&) = delete;
    Lock& operator=(const Lock&) = delete;

private:
    int fd_ = -1;
};

namespace {

// On-disk entry. `enqueued_at_ms` is the position in line: it survives lease
// expiry and interruption releases, so a job handed back after an interruption
// keeps its place instead of going to the back (SQS behaves the same way when a
// message's visibility timeout is reset). A failure-release counts as a new
// message and gets a new position, as with SQS SendMessage.
struct Entry {
    Job job;
    std::int64_t enqueued_at_ms = 0;
    std::int64_t lease_until_ms = 0;  // inflight only
    std::string token;                // inflight only
};

Entry read_entry(const fs::path& path) {
    const json root = json::parse(read_file(path));
    Entry entry;
    entry.job = job_from_json(root.at("job").dump());
    entry.enqueued_at_ms = root.value("enqueued_at_ms", std::int64_t{0});
    entry.lease_until_ms = root.value("lease_until_ms", std::int64_t{0});
    entry.token = root.value("lease_token", std::string());
    return entry;
}

void write_entry(const fs::path& path, const Entry& entry) {
    json root = {{"job", json::parse(job_to_json(entry.job))},
                 {"enqueued_at_ms", entry.enqueued_at_ms}};
    if (!entry.token.empty()) {
        root["lease_until_ms"] = entry.lease_until_ms;
        root["lease_token"] = entry.token;
    }
    write_file_atomically(path, root.dump());
}

std::int64_t lease_deadline(std::chrono::seconds lease) {
    return unix_millis() + std::chrono::duration_cast<std::chrono::milliseconds>(lease).count();
}

bool is_entry_file(const fs::directory_entry& entry) {
    return entry.is_regular_file() && entry.path().extension() == ".json";
}

}  // namespace

LocalQueue::LocalQueue(const std::string& queue_dir)
    : root_(queue_dir),
      pending_(root_ / "pending"),
      inflight_(root_ / "inflight"),
      dead_(root_ / "dead"),
      lock_path_(root_ / "queue.lock") {
    fs::create_directories(pending_);
    fs::create_directories(inflight_);
    fs::create_directories(dead_);
}

fs::path LocalQueue::inflight_path(const std::string& job_id) const {
    return inflight_ / (job_id + ".json");
}

void LocalQueue::push(const Job& job) {
    Entry entry;
    // Round-trip validates the job ID before it becomes a file name.
    entry.job = job_from_json(job_to_json(job));
    entry.enqueued_at_ms = unix_millis();
    Lock lock(lock_path_);
    const std::string file = entry.job.job_id + ".json";
    if (fs::exists(pending_ / file) || fs::exists(inflight_ / file) || fs::exists(dead_ / file)) {
        throw std::invalid_argument("job " + entry.job.job_id + " already exists in the queue");
    }
    write_entry(pending_ / file, entry);
}

void LocalQueue::requeue_expired() const {
    const std::int64_t now = unix_millis();
    for (const auto& item : fs::directory_iterator(inflight_)) {
        if (!is_entry_file(item)) {
            continue;
        }
        try {
            Entry entry = read_entry(item.path());
            if (entry.lease_until_ms <= now) {
                entry.token.clear();
                write_entry(pending_ / item.path().filename(), entry);
                fs::remove(item.path());
            }
        } catch (const std::exception&) {
            // A corrupt entry would block the job forever; surface it in dead/.
            fs::rename(item.path(), dead_ / item.path().filename());
        }
    }
}

std::optional<Job> LocalQueue::receive(std::chrono::seconds lease) {
    Lock lock(lock_path_);
    requeue_expired();

    std::optional<Entry> best;
    fs::path best_path;
    for (const auto& item : fs::directory_iterator(pending_)) {
        if (!is_entry_file(item)) {
            continue;
        }
        Entry entry = read_entry(item.path());
        if (!best || entry.enqueued_at_ms < best->enqueued_at_ms ||
            (entry.enqueued_at_ms == best->enqueued_at_ms && item.path() < best_path)) {
            best = std::move(entry);
            best_path = item.path();
        }
    }
    if (!best) {
        return std::nullopt;
    }

    best->lease_until_ms = lease_deadline(lease);
    best->token = random_hex(16);
    write_entry(inflight_path(best->job.job_id), *best);
    fs::remove(best_path);

    Job job = best->job;
    job.receipt = best->token;
    return job;
}

void LocalQueue::verify_lease(const Job& job) const {
    const fs::path path = inflight_path(job.job_id);
    if (!fs::exists(path)) {
        throw LeaseLostError("lease on " + job.job_id + " was lost (not in flight)");
    }
    const Entry entry = read_entry(path);
    if (entry.token != job.receipt) {
        throw LeaseLostError("lease on " + job.job_id + " was lost (claimed by another consumer)");
    }
    if (entry.lease_until_ms <= unix_millis()) {
        throw LeaseLostError("lease on " + job.job_id + " expired");
    }
}

void LocalQueue::extend_lease(const Job& job, std::chrono::seconds lease) {
    Lock lock(lock_path_);
    verify_lease(job);
    Entry entry = read_entry(inflight_path(job.job_id));
    entry.lease_until_ms = lease_deadline(lease);
    write_entry(inflight_path(job.job_id), entry);
}

void LocalQueue::complete(const Job& job) {
    Lock lock(lock_path_);
    verify_lease(job);
    fs::remove(inflight_path(job.job_id));
}

void LocalQueue::release(const Job& job, bool count_failure) {
    Lock lock(lock_path_);
    verify_lease(job);
    Entry entry = read_entry(inflight_path(job.job_id));
    entry.token.clear();
    if (count_failure) {
        ++entry.job.attempts;
        entry.enqueued_at_ms = unix_millis();  // a retry goes to the back of the line
    }
    // Write the pending copy before removing the claim. If we crash between the
    // two steps the job exists twice (harmless: at-least-once) rather than zero
    // times (a lost job).
    write_entry(pending_ / (job.job_id + ".json"), entry);
    fs::remove(inflight_path(job.job_id));
}

void LocalQueue::dead_letter(const Job& job) {
    Lock lock(lock_path_);
    verify_lease(job);
    Entry entry = read_entry(inflight_path(job.job_id));
    entry.token.clear();
    ++entry.job.attempts;
    write_entry(dead_ / (job.job_id + ".json"), entry);
    fs::remove(inflight_path(job.job_id));
}

}  // namespace spot_runner
