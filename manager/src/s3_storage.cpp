#include "spot_runner/s3_storage.h"

#include <aws/core/client/ClientConfiguration.h>
#include <aws/s3/S3Client.h>
#include <aws/s3/model/DeleteObjectRequest.h>
#include <aws/s3/model/GetObjectRequest.h>
#include <aws/s3/model/HeadObjectRequest.h>
#include <aws/s3/model/PutObjectRequest.h>

#include <filesystem>
#include <fstream>
#include <stdexcept>

#include "spot_runner/util.h"

namespace spot_runner {

S3Storage::S3Storage(const std::string& region, std::string bucket, std::string key_prefix)
    : bucket_(std::move(bucket)), prefix_(std::move(key_prefix)) {
    Aws::Client::ClientConfiguration config;
    config.region = region;
    config.connectTimeoutMs = 3000;
    config.requestTimeoutMs = 30000;
    client_ = std::make_unique<Aws::S3::S3Client>(config);
}

// Defined here, where S3Client is a complete type, so unique_ptr can delete it.
S3Storage::~S3Storage() = default;

void S3Storage::put_file(const std::string& key, const std::string& local_path) {
    Aws::S3::Model::PutObjectRequest request;
    request.SetBucket(bucket_);
    request.SetKey(full_key(key));
    auto body = Aws::MakeShared<Aws::FStream>("spot-runner", local_path.c_str(),
                                              std::ios_base::in | std::ios_base::binary);
    if (!body->good()) {
        throw std::runtime_error("cannot read " + local_path);
    }
    request.SetBody(body);
    const auto outcome = client_->PutObject(request);
    if (!outcome.IsSuccess()) {
        throw std::runtime_error("S3 PutObject " + full_key(key) + ": " +
                                 outcome.GetError().GetMessage());
    }
}

bool S3Storage::get_file(const std::string& key, const std::string& local_path) {
    Aws::S3::Model::GetObjectRequest request;
    request.SetBucket(bucket_);
    request.SetKey(full_key(key));
    auto outcome = client_->GetObject(request);
    if (!outcome.IsSuccess()) {
        if (outcome.GetError().GetErrorType() == Aws::S3::S3Errors::NO_SUCH_KEY) {
            return false;
        }
        throw std::runtime_error("S3 GetObject " + full_key(key) + ": " +
                                 outcome.GetError().GetMessage());
    }
    std::ostringstream contents;
    contents << outcome.GetResult().GetBody().rdbuf();
    write_file_atomically(local_path, contents.str());
    return true;
}

bool S3Storage::exists(const std::string& key) {
    Aws::S3::Model::HeadObjectRequest request;
    request.SetBucket(bucket_);
    request.SetKey(full_key(key));
    const auto outcome = client_->HeadObject(request);
    if (outcome.IsSuccess()) {
        return true;
    }
    const auto& error = outcome.GetError();
    if (error.GetErrorType() == Aws::S3::S3Errors::NO_SUCH_KEY ||
        error.GetErrorType() == Aws::S3::S3Errors::RESOURCE_NOT_FOUND ||
        error.GetResponseCode() == Aws::Http::HttpResponseCode::NOT_FOUND) {
        return false;
    }
    throw std::runtime_error("S3 HeadObject " + full_key(key) + ": " + error.GetMessage());
}

void S3Storage::remove(const std::string& key) {
    Aws::S3::Model::DeleteObjectRequest request;
    request.SetBucket(bucket_);
    request.SetKey(full_key(key));
    const auto outcome = client_->DeleteObject(request);
    if (!outcome.IsSuccess()) {
        throw std::runtime_error("S3 DeleteObject " + full_key(key) + ": " +
                                 outcome.GetError().GetMessage());
    }
}

}  // namespace spot_runner
