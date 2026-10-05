// =============================================================================
// test_local_storage.cpp (PHASE 4)
// =============================================================================
// Use a fresh temp folder per test so tests never affect each other:
//   GoogleTest "fixture": class LocalStorageTest : public ::testing::Test
//     SetUp():    make temp_directory_path() / "spot_storage_<random or test name>"
//     TearDown(): filesystem::remove_all(that folder)
//   Then write tests with TEST_F(LocalStorageTest, Name) instead of TEST.
//
// TESTS TO WRITE:
//   1. PutThenGetReturnsSameBytes: write a file, put_file, get_file to a new path,
//      compare contents.
//   2. GetMissingReturnsFalse: get_file on an unknown key returns false (no throw).
//   3. ExistsAndRemove: exists true after put, false after remove.
//   4. PutOverwrites: put twice with different contents; get returns the second.
//   5. NoTmpFileLeftBehind: after put, there's no "*.tmp" in the storage folder.
//   6. RejectsPathTraversal: put_file("../../evil", ...) throws invalid_argument.
//   7. KeyHelpers: checkpoint_key("job-001") == "jobs/job-001/checkpoint.dat".
// =============================================================================
