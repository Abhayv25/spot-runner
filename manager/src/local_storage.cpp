// =============================================================================
// local_storage.cpp: fake S3 in a folder (PHASE 4)
// =============================================================================
// STEP 1: Includes: "spot_runner/local_storage.h", <filesystem>, <stdexcept>
//   using namespace std; namespace fs = std::filesystem; namespace spot_runner { ... }
//
// STEP 2: checkpoint_key / result_key (from storage.h)
//   return "jobs/" + job_id + "/checkpoint.dat";   (and result.dat)
//
// STEP 3: Constructor: save root_dir_, fs::create_directories(root_dir_).
//
// STEP 4: path_for(key): return root_dir_ + "/" + key.
//   Safety check: if key contains ".." throw invalid_argument. Otherwise a bad key
//   could write files outside the storage folder ("path traversal", a real security bug).
//
// STEP 5: put_file(key, local_path)
//   - If local_path doesn't exist, throw runtime_error.
//   - final = path_for(key); create its parent folder (fs::path(final).parent_path()).
//   - Copy to final + ".tmp" (fs::copy_file with copy_options::overwrite_existing),
//     then fs::rename(tmp, final). Same all-or-nothing trick as the job.
//     (Real S3 already works this way: an upload is either fully there or not at all.)
//
// STEP 6: get_file(key, local_path)
//   - If path_for(key) doesn't exist, return false.
//   - Create local_path's parent folder, copy (overwrite), return true.
//
// STEP 7: exists(key): fs::exists(path_for(key))
// STEP 8: remove(key): fs::remove(path_for(key)) (no error if already gone)
// =============================================================================
