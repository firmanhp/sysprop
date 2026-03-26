include(FetchContent)

# ── Google Test + Google Mock ──────────────────────────────────────────────────
if(SYSPROP_BUILD_TESTS)
    # Prefer system-installed gtest if available
    find_package(GTest QUIET)
    if(NOT GTest_FOUND)
        message(STATUS "GTest not found via find_package, fetching from GitHub")
        FetchContent_Declare(
            googletest
            GIT_REPOSITORY https://github.com/google/googletest.git
            GIT_TAG        v1.14.0
            GIT_SHALLOW    TRUE
        )
        # Prevent googletest from overriding our compiler/linker settings
        set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
        FetchContent_MakeAvailable(googletest)
    endif()
endif()

# ── Google Benchmark ───────────────────────────────────────────────────────────
if(SYSPROP_BUILD_BENCHMARKS)
    find_package(benchmark QUIET)
    if(NOT benchmark_FOUND)
        message(STATUS "benchmark not found via find_package, fetching from GitHub")
        set(BENCHMARK_ENABLE_TESTING    OFF CACHE BOOL "" FORCE)
        set(BENCHMARK_ENABLE_INSTALL    OFF CACHE BOOL "" FORCE)
        set(BENCHMARK_DOWNLOAD_DEPENDENCIES ON CACHE BOOL "" FORCE)
        FetchContent_Declare(
            googlebenchmark
            GIT_REPOSITORY https://github.com/google/benchmark.git
            GIT_TAG        v1.8.3
            GIT_SHALLOW    TRUE
        )
        FetchContent_MakeAvailable(googlebenchmark)
    endif()
endif()
