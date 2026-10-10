# spot_runner_core holds everything except main() so the tests link the same code.
add_library(spot_runner_core STATIC
    src/config.cpp
    src/docker_client.cpp
    src/event_log.cpp
    src/file_watcher.cpp
    src/http_client.cpp
    src/imds_watcher.cpp
    src/job_queue.cpp
    src/job_runner.cpp
    src/local_queue.cpp
    src/local_storage.cpp
    src/logger.cpp
    src/process_runtime.cpp
    src/util.cpp
)

target_include_directories(spot_runner_core PUBLIC include)
target_link_libraries(spot_runner_core
    PUBLIC nlohmann_json::nlohmann_json Threads::Threads
    PRIVATE spot_runner_warnings
)

if(SPOT_RUNNER_ENABLE_AWS)
    # A statically linked SDK exports ZLIB::ZLIB without finding it.
    find_package(ZLIB)
    find_package(AWSSDK REQUIRED COMPONENTS s3 sqs)
    target_sources(spot_runner_core PRIVATE src/s3_storage.cpp src/sqs_queue.cpp)
    target_link_libraries(spot_runner_core PUBLIC ${AWSSDK_LINK_LIBRARIES})
    target_compile_definitions(spot_runner_core PUBLIC SPOT_RUNNER_WITH_AWS=1)
endif()

add_executable(spot-runner src/main.cpp)
target_link_libraries(spot-runner PRIVATE spot_runner_core spot_runner_warnings)
install(TARGETS spot-runner RUNTIME DESTINATION bin)
