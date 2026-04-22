cmake_minimum_required(VERSION 3.16)
project(sysprop VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ── Build options ──────────────────────────────────────────────────────────────
option(SYSPROP_BUILD_TOOLS      "Build CLI tools"    ON)
option(SYSPROP_BUILD_TESTS      "Build unit tests"   OFF)
option(SYSPROP_BUILD_BENCHMARKS "Build benchmarks"   OFF)
option(SYSPROP_BUILD_EXAMPLES   "Build examples"     OFF)

# ── Configurable defaults ──────────────────────────────────────────────────────
set(SYSPROP_RUNTIME_DIR      "/run/sysprop/props"      CACHE STRING "Volatile property directory (cleared on reboot)")
set(SYSPROP_PERSISTENT_DIR   "/etc/sysprop/persistent" CACHE STRING "Persistent property directory (survives reboot); empty string disables persistence")
set(SYSPROP_MAX_KEY_LENGTH   256                        CACHE STRING "Property key buffer size in bytes, including null terminator")
set(SYSPROP_MAX_VALUE_LENGTH 256                        CACHE STRING "Property value buffer size in bytes, including null terminator")
# SYSPROP_ENABLE_PERSISTENCE is derived: 1 when SYSPROP_PERSISTENT_DIR is non-empty, else 0.
set(SYSPROP_ENABLE_PERSISTENCE 1)
if(SYSPROP_PERSISTENT_DIR STREQUAL "")
    set(SYSPROP_ENABLE_PERSISTENCE 0)
endif()

# ── Compile flags ──────────────────────────────────────────────────────────────
# Single build configuration: no Debug/Release split. Flags are explicit and
# applied only to our own targets via this INTERFACE library. Third-party code
# brought in by FetchContent is never linked against it and inherits no flags.
add_library(sysprop_build_flags INTERFACE)
target_compile_options(sysprop_build_flags INTERFACE
    -O3 -Wall -Wextra -Wpedantic -Werror -fno-exceptions
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
