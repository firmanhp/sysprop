# C example — uses the C API (sysprop_get / sysprop_set / ...)
add_executable(example_c c_example/main.c)
target_link_libraries(example_c PRIVATE sysprop sysprop_build_flags)
set_target_properties(example_c PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
)

# C++ example — uses the namespace wrappers (sysprop::Get / sysprop::Set / ...)
add_executable(example_cpp cpp_example/main.cpp)
target_link_libraries(example_cpp PRIVATE sysprop sysprop_build_flags)
