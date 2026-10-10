#include "spot_runner/job_queue.h"

#include <algorithm>
#include <cctype>
#include <stdexcept>

#include <nlohmann/json.hpp>

namespace spot_runner {

namespace {

using nlohmann::json;

// Job IDs become file names, S3 key segments, and container names, so they are
// restricted to a conservative character set.
bool is_valid_job_id(const std::string& id) {
    if (id.empty() || id.size() > 128 || id.front() == '.' || id.front() == '-') {
        return false;
    }
    return std::all_of(id.begin(), id.end(), [](unsigned char c) {
        return std::isalnum(c) || c == '-' || c == '_' || c == '.';
    });
}

}  // namespace

std::string job_to_json(const Job& job) {
    json env = json::object();
    for (const auto& [key, value] : job.env) {
        env[key] = value;
    }
    return json{{"job_id", job.job_id}, {"image", job.image}, {"env", env}, {"attempts", job.attempts}}
        .dump();
}

Job job_from_json(const std::string& text) {
    const json root = json::parse(text, nullptr, false);
    if (root.is_discarded() || !root.is_object()) {
        throw std::invalid_argument("job message is not a JSON object");
    }
    Job job;
    try {
        job.job_id = root.at("job_id").get<std::string>();
        job.image = root.at("image").get<std::string>();
        job.attempts = root.value("attempts", 0);
        if (root.contains("env")) {
            for (const auto& [key, value] : root.at("env").items()) {
                job.env[key] = value.is_string() ? value.get<std::string>() : value.dump();
            }
        }
    } catch (const json::exception& e) {
        throw std::invalid_argument(std::string("invalid job message: ") + e.what());
    }
    if (!is_valid_job_id(job.job_id)) {
        throw std::invalid_argument("invalid job_id '" + job.job_id +
                                    "' (use letters, digits, '-', '_', '.')");
    }
    if (job.image.empty()) {
        throw std::invalid_argument("job " + job.job_id + " has an empty image");
    }
    if (job.attempts < 0) {
        throw std::invalid_argument("job " + job.job_id + " has negative attempts");
    }
    return job;
}

}  // namespace spot_runner
