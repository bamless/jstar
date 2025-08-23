# Executable
add_executable(jstarc main.c)
target_link_libraries(jstarc PRIVATE jstar common argparse)
target_include_directories(jstarc PRIVATE ${PROJECT_BINARY_DIR})

# Enable link-time optimization if supported
if(LTO)
    set_target_properties(jstarc PROPERTIES INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()

# Install target
if(JSTAR_INSTALL)
    include(GNUInstallDirs)

    # Setup relative rpath on unix and macos
    if(APPLE)
        set_target_properties(jstarc PROPERTIES INSTALL_RPATH "@executable_path/../${CMAKE_INSTALL_LIBDIR}")
    elseif(UNIX)
        set_target_properties(jstarc PROPERTIES INSTALL_RPATH "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}")
    endif()

    install(TARGETS jstarc
        EXPORT jstar-export
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
endif()
