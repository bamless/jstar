# Executable
add_executable(jstarc main.c)
target_link_libraries(jstarc PRIVATE jstar common argparse)
target_include_directories(jstarc PRIVATE ${PROJECT_BINARY_DIR} ${PROJECT_SOURCE_DIR}/profile)
if(WIN32)
    target_link_libraries(jstarc PRIVATE dirent)
endif()

if(JSTAR_INSTRUMENT)
    target_link_libraries(jstarc PRIVATE profile)
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
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
endif()
