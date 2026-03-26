cmake_minimum_required(VERSION 3.16)
project(sysprop VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ── Build options ──────────────────────────────────────────────────────────────
option(SYSPROP_BUILD_TOOLS      "Build CLI tools"    ON)
option(SYSPROP_BUILD_TESTS      "Build unit tests"   OFF)
option(SYSPROP_BUILD_BENCHMARKS "Build benchmarks"   OFF)
option(SYSPROP_BUILD_EXAMPLES   "Build examples"     OFF)

# ── Configurable defaults ──────────────────────────────────────────────────────
set(SYSPROP_RUNTIME_DIR    "/run/sysprop/props"       CACHE STRING "Default runtime property directory")
set(SYSPROP_PERSISTENT_DIR "/etc/sysprop/persistent"  CACHE STRING "Default persistent property directory")
set(SYSPROP_MAX_KEY_LENGTH   256 CACHE STRING "Maximum property key length in bytes")
set(SYSPROP_MAX_VALUE_LENGTH 256 CACHE STRING "Maximum property value length in bytes")

# ── Compile flags ──────────────────────────────────────────────────────────────
# Single build configuration: no Debug/Release split. Flags are explicit and
# applied only to our own targets via this INTERFACE library. Third-party code
# brought in by FetchContent is never linked against it and inherits no flags.
add_library(sysprop_build_flags INTERFACE)
target_compile_options(sysprop_build_flags INTERFACE
    -O3 -Wall -Wextra -Wpedantic -Werror
)

# ── Dependencies ───────────────────────────────────────────────────────────────
include(cmake/FetchDependencies.cmake)

# ── Subdirectories ─────────────────────────────────────────────────────────────
add_subdirectory(src)

if(SYSPROP_BUILD_TOOLS)
    add_subdirectory(tools)
endif()

if(SYSPROP_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(SYSPROP_BUILD_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()

if(SYSPROP_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()
