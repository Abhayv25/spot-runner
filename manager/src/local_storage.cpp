#include "spot_runner/local_storage.h"

#include <stdexcept>

#include "spot_runner/util.h"

namespace spot_runner {

std::string checkpoint_key(const std::string& job_id) {
    return "jobs/" + job_id + "/checkpoint.dat";
}

std::string result_key(const std::string& job_id) {
    return "jobs/" + job_id + "/result.dat";
}

LocalStorage::LocalStorage(const std::string& root_dir) : root_(root_dir) {
    std::filesystem::create_directories(root_);
}

std::filesystem::path LocalStorage::path_for(const std::string& key) const {
    const std::filesystem::path relative(key);
    if (key.empty() || relative.is_absolute()) {
        throw std::invalid_argument("invalid storage key: '" + key + "'");
    }
    for (const auto& part : relative) {
        if (part == "..") {
            throw std::invalid_argument("storage key must not contain '..': " + key);
        }
    }
    return root_ / relative;
}

void LocalStorage::put_file(const std::string& key, const std::string& local_path) {
    copy_file_atomically(local_path, path_for(key));
}

bool LocalStorage::get_file(const std::string& key, const std::string& local_path) {
    const auto source = path_for(key);
    if (!std::filesystem::exists(source)) {
        return false;
    }
    copy_file_atomically(source, local_path);
    return true;
}

bool LocalStorage::exists(const std::string& key) {
    return std::filesystem::exists(path_for(key));
}

void LocalStorage::remove(const std::string& key) {
    std::error_code ec;
    std::filesystem::remove(path_for(key), ec);
}

}  // namespace spot_runner
