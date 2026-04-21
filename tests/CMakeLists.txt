add_executable(sysprop_tests
    validation_test.cpp
    file_backend_test.cpp
    file_property_store_test.cpp
    mock_property_store_test.cpp
    integration_test.cpp
    sysprop_init_test.cpp
    sysprop_main_test.cpp
    sysprop_init_helpers_test.cpp
)

target_link_libraries(sysprop_tests
    PRIVATE
        sysprop
        sysprop_defaults_loader
        sysprop_cli_commands
        sysprop_init_helpers
        sysprop_build_flags
        GTest::gtest_main
)

target_include_directories(sysprop_tests
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src
        ${PROJECT_SOURCE_DIR}/tools
)

include(GoogleTest)
gtest_discover_tests(sysprop_tests)
