add_executable(sysprop_tests
    validation_test.cpp
    file_backend_test.cpp
    property_store_test.cpp
    integration_test.cpp
)

target_link_libraries(sysprop_tests
    PRIVATE
        sysprop
        sysprop_build_flags
        GTest::gtest_main
        GTest::gmock
)

target_include_directories(sysprop_tests
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src
)

include(GoogleTest)
gtest_discover_tests(sysprop_tests)
