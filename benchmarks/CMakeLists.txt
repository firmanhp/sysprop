add_executable(sysprop_benchmark sysprop_benchmark.cpp)

target_link_libraries(sysprop_benchmark
    PRIVATE
        sysprop
        sysprop_build_flags
        benchmark::benchmark_main
)

target_include_directories(sysprop_benchmark
    PRIVATE ${PROJECT_SOURCE_DIR}/src
)
