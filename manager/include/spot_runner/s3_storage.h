// =============================================================================
// s3_storage.h: Storage backed by Amazon S3 (PHASE 7)
// =============================================================================
// Only compiled when SPOT_RUNNER_ENABLE_AWS is ON (see manager/CMakeLists.txt).
// Uses the AWS SDK for C++ (you install it in Phase 7; see infra/README.md).
//
// STEP 1: #pragma once, include "spot_runner/storage.h", <memory>, namespace spot_runner
//   Forward-declare the SDK client so this header doesn't drag the whole SDK into
//   every file:  namespace Aws::S3 { class S3Client; }
//
// STEP 2: class S3Storage : public Storage
//   public:
//     S3Storage(const std::string& region, const std::string& bucket);
//     ~S3Storage() override;   // defined in the .cpp (needed for unique_ptr of a
//                              // forward-declared type; look up "pimpl idiom")
//     The 4 interface functions with "override".
//   private:
//     std::string bucket_;
//     std::unique_ptr<Aws::S3::S3Client> client_;
// =============================================================================
