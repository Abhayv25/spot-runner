cmake_minimum_required(VERSION 3.20)

project(spot-runner
    VERSION 0.1.0
    DESCRIPTION "Interruption-tolerant job runner for cheap (spot) cloud computers"
    LANGUAGES CXX)

# ---------------------------------------------------------------------------
# Global C++ settings
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Writes build/<preset>/compile_commands.json so VS Code understands your includes.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

# ---------------------------------------------------------------------------
# Options you can flip with -D... or in CMakePresets.json
# ---------------------------------------------------------------------------
option(SPOT_RUNNER_BUILD_TESTS "Build the unit tests" ON)

# OFF until Phase 7 of the README. When you turn it ON, CMake looks for the
# AWS SDK for C++ (you install it yourself) and compiles the S3 / SQS code.
option(SPOT_RUNNER_ENABLE_AWS "Build the AWS-backed storage and queue" OFF)

# ---------------------------------------------------------------------------
# Warning flags shared by every target in this repo
# ---------------------------------------------------------------------------
add_library(spot_runner_warnings INTERFACE)
if(MSVC)
    target_compile_options(spot_runner_warnings INTERFACE /W4)
else()
    target_compile_options(spot_runner_warnings INTERFACE
        -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wno-sign-conversion)
endif()

# ---------------------------------------------------------------------------
# Third-party libraries (downloaded automatically the first time you configure)
# ---------------------------------------------------------------------------
include(FetchContent)

# JSON parsing for the manager (config file, Docker API replies, job messages).
FetchContent_Declare(nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(nlohmann_json)

# ---------------------------------------------------------------------------
# Sub-projects
# ---------------------------------------------------------------------------
add_subdirectory(jobs/prime-counter)
add_subdirectory(manager)

if(SPOT_RUNNER_BUILD_TESTS)
    FetchContent_Declare(googletest
        URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.tar.gz
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    # Keep GoogleTest from overriding our compiler settings on Windows.
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)

    enable_testing()
    add_subdirectory(tests)
endif()
