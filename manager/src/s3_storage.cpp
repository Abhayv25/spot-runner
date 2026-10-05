// =============================================================================
// s3_storage.cpp: Storage on Amazon S3 (PHASE 7)
// =============================================================================
// Only compiled with the "debug-aws" preset (SPOT_RUNNER_ENABLE_AWS=ON).
// Before this: finish infra/README.md steps 1-4 (account, AWS CLI, bucket, SDK install).
//
// STEP 1: Includes
//   "spot_runner/s3_storage.h"
//   <aws/s3/S3Client.h>, <aws/s3/model/PutObjectRequest.h>,
//   <aws/s3/model/GetObjectRequest.h>, <aws/s3/model/HeadObjectRequest.h>,
//   <aws/s3/model/DeleteObjectRequest.h>, <aws/core/client/ClientConfiguration.h>,
//   <fstream>
//   using namespace std; namespace spot_runner { ... }
//
// STEP 2: Constructor
//   Aws::Client::ClientConfiguration cfg; cfg.region = region;
//   client_ = make_unique<Aws::S3::S3Client>(cfg);
//   NO keys in code, ever. The SDK finds credentials by itself: on your Mac from
//   "aws configure"; on EC2 from the instance's IAM role. (Interview point!)
//
// STEP 3: Destructor: "S3Storage::~S3Storage() = default;"  (must be here, in the .cpp)
//
// STEP 4: put_file(key, local_path)
//   PutObjectRequest req; req.SetBucket(bucket_); req.SetKey(key);
//   Body: make_shared<Aws::FStream>(local_path, ios_base::in | ios_base::binary)
//         and req.SetBody(stream)
//   auto outcome = client_->PutObject(req);
//   if (!outcome.IsSuccess()) throw runtime_error(outcome.GetError().GetMessage());
//   S3 uploads are already all-or-nothing, so no tmp/rename needed here.
//
// STEP 5: get_file(key, local_path)
//   GetObjectRequest. If the error type is NO_SUCH_KEY, return false.
//   Otherwise copy outcome.GetResult().GetBody() into an ofstream at local_path
//   (write to local_path + ".tmp", then rename, same as everywhere else).
//
// STEP 6: exists(key): HeadObjectRequest; success -> true; NO_SUCH_KEY / 404 -> false.
// STEP 7: remove(key): DeleteObjectRequest.
//
// SDK lifecycle: the AWS SDK must be started once before ANY client is made and shut
// down at the end: Aws::InitAPI(options) / Aws::ShutdownAPI(options). Do that in
// main.cpp, not here.
// =============================================================================
