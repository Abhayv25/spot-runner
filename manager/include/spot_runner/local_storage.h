// =============================================================================
// local_storage.h: Storage backed by a folder (PHASE 4)
// =============================================================================
// Pretends to be S3 using ./run/storage on your Mac. Everything above this layer
// can't tell the difference, which is the whole point.
//
// STEP 1: #pragma once, include "spot_runner/storage.h", namespace spot_runner
//
// STEP 2: class LocalStorage : public Storage
//   public:
//     explicit LocalStorage(const std::string& root_dir);  // creates the folder if needed
//     The 4 interface functions with "override".
//   private:
//     std::string path_for(const std::string& key) const;  // root_dir + "/" + key
//     std::string root_dir_;
// =============================================================================
