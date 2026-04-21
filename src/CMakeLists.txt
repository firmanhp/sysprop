add_library(sysprop STATIC
    validation.cpp
    file_backend.cpp
    file_property_store.cpp
    sysprop.cpp
)

target_include_directories(sysprop
    PUBLIC  ${PROJECT_SOURCE_DIR}/include
    PRIVATE ${PROJECT_SOURCE_DIR}/src
)

# Expose the configured path/size constants as compile definitions
target_compile_definitions(sysprop PUBLIC
    SYSPROP_RUNTIME_DIR="${SYSPROP_RUNTIME_DIR}"
    SYSPROP_PERSISTENT_DIR="${SYSPROP_PERSISTENT_DIR}"
    SYSPROP_MAX_KEY_LENGTH=${SYSPROP_MAX_KEY_LENGTH}
    SYSPROP_MAX_VALUE_LENGTH=${SYSPROP_MAX_VALUE_LENGTH}
)

target_link_libraries(sysprop
    PRIVATE
        sysprop_build_flags
)
