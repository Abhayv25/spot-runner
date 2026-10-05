add_executable(spot_runner_tests
    test_config.cpp
    test_http_parsing.cpp
    test_local_storage.cpp
    test_local_queue.cpp
    test_job_runner.cpp
)

# "fakes/" holds pretend versions of Docker and the interruption watcher.
target_include_directories(spot_runner_tests PRIVATE .)

target_link_libraries(spot_runner_tests PRIVATE
    spot_runner_core
    spot_runner_warnings
    GTest::gtest_main
)

# Lets "ctest" discover every TEST(...) you write automatically.
include(GoogleTest)
gtest_discover_tests(spot_runner_tests
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    DISCOVERY_TIMEOUT 30)
