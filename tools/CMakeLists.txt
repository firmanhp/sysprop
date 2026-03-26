add_library(sysprop_defaults_loader STATIC defaults_loader.cpp)
target_include_directories(sysprop_defaults_loader
    PUBLIC  ${PROJECT_SOURCE_DIR}/tools
    PRIVATE ${PROJECT_SOURCE_DIR}/src
            ${PROJECT_SOURCE_DIR}/include
)
target_link_libraries(sysprop_defaults_loader PRIVATE sysprop sysprop_build_flags)

add_library(sysprop_cli_commands STATIC cli_commands.cpp)
target_include_directories(sysprop_cli_commands
    PUBLIC  ${PROJECT_SOURCE_DIR}/tools
    PRIVATE ${PROJECT_SOURCE_DIR}/src
            ${PROJECT_SOURCE_DIR}/include
)
target_link_libraries(sysprop_cli_commands PUBLIC sysprop sysprop_build_flags)
target_include_directories(sysprop_cli_commands PRIVATE ${PROJECT_SOURCE_DIR}/src)

add_library(sysprop_init_helpers STATIC init_helpers.cpp)
target_include_directories(sysprop_init_helpers
    PUBLIC  ${PROJECT_SOURCE_DIR}/tools
            ${PROJECT_SOURCE_DIR}/include
)
target_link_libraries(sysprop_init_helpers PRIVATE sysprop_build_flags)

add_executable(sysprop_bin sysprop_main.cpp)
set_target_properties(sysprop_bin PROPERTIES OUTPUT_NAME sysprop)
target_link_libraries(sysprop_bin PRIVATE sysprop sysprop_cli_commands sysprop_build_flags)
target_include_directories(sysprop_bin PRIVATE ${PROJECT_SOURCE_DIR}/src)

add_executable(sysprop-init sysprop_init.cpp)
target_link_libraries(sysprop-init PRIVATE sysprop sysprop_defaults_loader sysprop_init_helpers sysprop_build_flags)
target_include_directories(sysprop-init PRIVATE ${PROJECT_SOURCE_DIR}/src)

# ── Install ────────────────────────────────────────────────────────────────────
include(GNUInstallDirs)

install(TARGETS sysprop_bin sysprop-init
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# Create getprop and setprop as symlinks to sysprop
install(CODE "
    set(bindir \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}\")
    foreach(link getprop setprop)
        set(target \"\${bindir}/\${link}\")
        if(EXISTS \"\${target}\")
            file(REMOVE \"\${target}\")
        endif()
        execute_process(COMMAND \${CMAKE_COMMAND} -E create_symlink sysprop \"\${target}\")
        message(STATUS \"Installing: \${target} -> sysprop\")
    endforeach()
")
