# Executable
add_executable(jstarc jstarc.c)
target_link_libraries(jstarc PRIVATE jstar instrument argparse cwalk common)
target_include_directories(jstarc PRIVATE ${PROJECT_SOURCE_DIR}/instrument)
if(WIN32)
    target_link_libraries(jstarc PRIVATE dirent)
endif()

# Enable link-time optimization if supported
if(LTO)
    set_target_properties(jstarc PROPERTIES INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()

# Install target
if(JSTAR_INSTALL)
    # Setup relative rpath on unix and macos
    if(APPLE)
        set_target_properties(jstarc PROPERTIES INSTALL_RPATH "@executable_path/../lib")
    elseif(UNIX)
        set_target_properties(jstarc PROPERTIES INSTALL_RPATH "$ORIGIN/../lib")
    endif()

    install(TARGETS jstarc
        EXPORT jstar-export
        RUNTIME DESTINATION bin
    )
endif()