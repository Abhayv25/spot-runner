// =============================================================================
// test_config.cpp (PHASE 3)
// =============================================================================
// How GoogleTest works (read once):
//   #include <gtest/gtest.h>
//   TEST(GroupName, TestName) { ...  EXPECT_EQ(a, b);  EXPECT_TRUE(x);
//                                    EXPECT_THROW(statement, ExceptionType); }
//   No main() needed (gtest_main provides it).
//   Run all tests:  ctest --preset debug     (or run build/debug/tests/spot_runner_tests)
//
// Helper you'll want in several test files: write a temp file and return its path.
//   Use filesystem::temp_directory_path() / "spot_runner_test_<something>.json"
//
// TESTS TO WRITE:
//   1. LoadsValidLocalConfig: load config/manager.local.json (path relative to the repo:
//      use the macro idea below) and check manager_id == "laptop-1", lease_seconds == 60.
//      Path tip: the tests run in build/debug/tests, so either copy the JSON text into a
//      temp file in the test, or pass the repo path in from CMake. Temp file is simpler.
//   2. MissingFileThrows: EXPECT_THROW(load_config("/nope.json"), std::runtime_error)
//   3. BrokenJsonThrows: file containing "{ not json" -> runtime_error
//   4. MissingRequiredKeyThrows: valid JSON with no "manager_id"
//   5. UsesDefaultsForMissingNumbers: no "poll_interval_ms" -> default value
//   6. RejectsBadMode: "mode": "cloud"
//   7. RejectsGraceTooLong: stop_grace_seconds = 115
//   8. RejectsLeaseTooShort: lease_seconds smaller than 2x the upload interval
// =============================================================================
