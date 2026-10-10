#pragma once

#include <memory>
#include <string>

#include "spot_runner/storage.h"

namespace Aws::S3 {
class S3Client;
}

namespace spot_runner {

// Storage backed by Amazon S3. A single PutObject is atomic: readers see the
// old object or the complete new one, which is exactly the Storage contract.
// Credentials come from the default AWS provider chain (the EC2 instance
// profile in production, ~/.aws locally); none are ever configured in code.
class S3Storage : public Storage {
public:
    S3Storage(const std::string& region, std::string bucket, std::string key_prefix);
    ~S3Storage() override;

    S3Storage(const S3Storage&) = delete;
    S3Storage& operator=(const S3Storage&) = delete;

    void put_file(const std::string& key, const std::string& local_path) override;
    bool get_file(const std::string& key, const std::string& local_path) override;
    bool exists(const std::string& key) override;
    void remove(const std::string& key) override;

private:
    std::string full_key(const std::string& key) const { return prefix_ + key; }

    std::string bucket_;
    std::string prefix_;
    std::unique_ptr<Aws::S3::S3Client> client_;
};

}  // namespace spot_runner
