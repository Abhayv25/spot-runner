#pragma once

#include <filesystem>
#include <string>

#include "spot_runner/util.h"

namespace spot_runner::testing {

// Creates a unique directory under the system temp dir and removes it on destruction.
class TempDir {
public:
    TempDir()
        : path_(std::filesystem::temp_directory_path() / ("spot-runner-test-" + random_hex(6))) {
        std::filesystem::create_directories(path_);
    }
    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path_, ec);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;

    const std::filesystem::path& path() const { return path_; }
    std::string str(const std::string& child = "") const {
        return child.empty() ? path_.string() : (path_ / child).string();
    }

private:
    std::filesystem::path path_;
};

}  // namespace spot_runner::testing
