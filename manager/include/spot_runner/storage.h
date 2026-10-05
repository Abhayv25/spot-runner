// =============================================================================
// storage.h: the INTERFACE for "where save files live" (PHASE 4)
// =============================================================================
// Real versions: LocalStorage (a folder on your Mac) and S3Storage (Amazon, Phase 7).
// Keys look like folder paths: "jobs/job-001/checkpoint.dat"
//
// STEP 1: #pragma once, include <string>, namespace spot_runner
//
// STEP 2: class Storage
//   public:
//     virtual ~Storage() = default;
//
//     // Upload a file from the computer. Must be all-or-nothing: someone reading
//     // the key sees the old version or the new one, never a half-written one.
//     virtual void put_file(const std::string& key, const std::string& local_path) = 0;
//
//     // Download to local_path. Return false (don't throw) if the key doesn't exist,
//     // because "no checkpoint yet" is normal, not an error.
//     virtual bool get_file(const std::string& key, const std::string& local_path) = 0;
//
//     virtual bool exists(const std::string& key) = 0;
//     virtual void remove(const std::string& key) = 0;
//
// STEP 3: Below the class, prototypes for two helpers so every file builds keys the same way:
//   std::string checkpoint_key(const std::string& job_id);   // "jobs/<id>/checkpoint.dat"
//   std::string result_key(const std::string& job_id);       // "jobs/<id>/result.dat"
//   (Define them in local_storage.cpp. They're tiny.)
// =============================================================================
