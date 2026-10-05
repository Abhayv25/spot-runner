# This file works two ways:
#   1. As part of the whole repo (the root CMakeLists.txt adds it).
#   2. On its own, so your Dockerfile can build ONLY the job without the manager:
#        cmake -S jobs/prime-counter -B build && cmake --build build
# The job has zero third-party dependencies on purpose: smaller image, faster build.

cmake_minimum_required(VERSION 3.20)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(prime-counter LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)
    if(NOT CMAKE_BUILD_TYPE)
        set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
    endif()
endif()

add_executable(prime-counter main.cpp)

if(TARGET spot_runner_warnings)
    target_link_libraries(prime-counter PRIVATE spot_runner_warnings)
endif()

install(TARGETS prime-counter RUNTIME DESTINATION bin)
