cmake_minimum_required(VERSION 3.20)

project(spot-runner
    VERSION 1.0.0
    DESCRIPTION "Interruption-tolerant job runner for EC2 Spot instances"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

option(SPOT_RUNNER_BUILD_TESTS "Build unit and integration tests" ON)
option(SPOT_RUNNER_ENABLE_AWS "Build the S3 and SQS backends (requires the AWS SDK for C++)" OFF)
option(SPOT_RUNNER_SANITIZE "Build with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(SPOT_RUNNER_WERROR "Treat compiler warnings as errors" OFF)

find_package(Threads REQUIRED)

# Shared warning and sanitizer flags, applied to first-party targets only.
add_library(spot_runner_warnings INTERFACE)
target_compile_options(spot_runner_warnings INTERFACE
    -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wno-sign-conversion
    -Wnon-virtual-dtor -Wold-style-cast -Wcast-align -Woverloaded-virtual)
if(SPOT_RUNNER_WERROR)
    target_compile_options(spot_runner_warnings INTERFACE -Werror)
endif()
if(SPOT_RUNNER_SANITIZE)
    target_compile_options(spot_runner_warnings INTERFACE
        -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(spot_runner_warnings INTERFACE -fsanitize=address,undefined)
endif()

include(FetchContent)

FetchContent_Declare(nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
    URL_HASH SHA256=42f6e95cad6ec532fd372391373363b62a14af6d771056dbfc86160e6dfff7aa
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(nlohmann_json)

add_subdirectory(jobs/prime-counter)
add_subdirectory(manager)

if(SPOT_RUNNER_BUILD_TESTS)
    FetchContent_Declare(googletest
        URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.tar.gz
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)

    enable_testing()
    add_subdirectory(tests)
endif()
