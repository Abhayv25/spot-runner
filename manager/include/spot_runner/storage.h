#pragma once

#include <string>

namespace spot_runner {

// Durable object storage for checkpoints and results. Keys are slash-separated
// paths such as "jobs/job-001/checkpoint.dat".
class Storage {
public:
    virtual ~Storage() = default;

    // Uploads a local file. Must be atomic: readers of `key` observe either the
    // previous object or the complete new one, never a partial upload.
    virtual void put_file(const std::string& key, const std::string& local_path) = 0;

    // Downloads `key` to `local_path`. Returns false if the key does not exist,
    // because "no checkpoint yet" is a normal state, not an error.
    virtual bool get_file(const std::string& key, const std::string& local_path) = 0;

    virtual bool exists(const std::string& key) = 0;
    virtual void remove(const std::string& key) = 0;
};

std::string checkpoint_key(const std::string& job_id);
std::string result_key(const std::string& job_id);

}  // namespace spot_runner
