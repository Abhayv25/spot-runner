add_executable(spot_runner_tests
    support/test_http_server.cpp
    test_config.cpp
    test_docker_client.cpp
    test_end_to_end.cpp
    test_http_client.cpp
    test_imds_watcher.cpp
    test_job_queue.cpp
    test_job_runner.cpp
    test_local_storage.cpp
    test_process_runtime.cpp
    test_util.cpp
)

target_include_directories(spot_runner_tests PRIVATE support)
target_link_libraries(spot_runner_tests PRIVATE
    spot_runner_core
    spot_runner_warnings
    GTest::gtest_main
)

# The end-to-end and process-runtime tests run the real prime-counter binary.
add_dependencies(spot_runner_tests prime-counter)
target_compile_definitions(spot_runner_tests PRIVATE
    PRIME_COUNTER_PATH="$<TARGET_FILE:prime-counter>")

include(GoogleTest)
gtest_discover_tests(spot_runner_tests DISCOVERY_TIMEOUT 30)
