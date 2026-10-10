#pragma once

#include <filesystem>
#include <string>

#include "spot_runner/storage.h"

namespace spot_runner {

// Storage backed by a local directory. Stands in for S3 during development and
// tests; uploads use temp-file + rename so they are atomic like S3 PUTs.
class LocalStorage : public Storage {
public:
    explicit LocalStorage(const std::string& root_dir);

    void put_file(const std::string& key, const std::string& local_path) override;
    bool get_file(const std::string& key, const std::string& local_path) override;
    bool exists(const std::string& key) override;
    void remove(const std::string& key) override;

private:
    // Rejects keys that would escape the root directory.
    std::filesystem::path path_for(const std::string& key) const;

    std::filesystem::path root_;
};

}  // namespace spot_runner
