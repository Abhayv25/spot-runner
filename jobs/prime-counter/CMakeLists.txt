# Builds standalone (used by the Dockerfile) or as part of the top-level project.
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

option(PRIME_COUNTER_STATIC "Link prime-counter statically (for FROM scratch images)" OFF)

add_executable(prime-counter main.cpp)

if(TARGET spot_runner_warnings)
    target_link_libraries(prime-counter PRIVATE spot_runner_warnings)
endif()

if(PRIME_COUNTER_STATIC)
    target_link_options(prime-counter PRIVATE -static)
endif()

install(TARGETS prime-counter RUNTIME DESTINATION bin)
