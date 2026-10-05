# ---------------------------------------------------------------------------
# spot_runner_core: everything except main(). The tests link against this too,
# so all real logic lives here and main.cpp stays tiny.
# ---------------------------------------------------------------------------
set(CORE_SOURCES
    src/config.cpp
    src/logger.cpp
    src/http_client.cpp
    src/docker_client.cpp
    src/local_storage.cpp
    src/local_queue.cpp
    src/file_watcher.cpp
    src/imds_watcher.cpp
    src/job_runner.cpp
)

if(SPOT_RUNNER_ENABLE_AWS)
    list(APPEND CORE_SOURCES
        src/s3_storage.cpp
        src/sqs_queue.cpp
    )
endif()

add_library(spot_runner_core STATIC ${CORE_SOURCES})

target_include_directories(spot_runner_core PUBLIC include)

target_link_libraries(spot_runner_core
    PUBLIC nlohmann_json::nlohmann_json
    PRIVATE spot_runner_warnings
)

if(SPOT_RUNNER_ENABLE_AWS)
    # Installed by you in Phase 7 (see infra/README.md).
    find_package(AWSSDK REQUIRED COMPONENTS s3 sqs)
    target_link_libraries(spot_runner_core PUBLIC ${AWSSDK_LINK_LIBRARIES})
    # Lets main.cpp use "#ifdef SPOT_RUNNER_WITH_AWS" to pick S3/SQS when available.
    target_compile_definitions(spot_runner_core PUBLIC SPOT_RUNNER_WITH_AWS=1)
endif()

# ---------------------------------------------------------------------------
# The manager program itself
# ---------------------------------------------------------------------------
add_executable(spot-runner src/main.cpp)
target_link_libraries(spot-runner PRIVATE spot_runner_core spot_runner_warnings)
