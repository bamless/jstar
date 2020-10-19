# setup relative rpath on unix and macos
if(APPLE)
    set(CMAKE_INSTALL_RPATH "@executable_path/../lib")
else()
    set(CMAKE_INSTALL_RPATH "$ORIGIN/../lib")
endif()

# CLI app
add_executable(cli cli.c)
target_link_libraries(cli PRIVATE jstar linenoise argparse)
set_target_properties(cli PROPERTIES OUTPUT_NAME "jstar")

# Enable link-time optimization if supported
if(LTO)
    set_target_properties(cli PROPERTIES INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()

# add icon to exe on windows
if(WIN32)
    target_sources(cli PRIVATE "icon.rc")
endif()

# Install target
if(JSTAR_INSTALL)
    install(TARGETS cli
        EXPORT jstar-export
        RUNTIME DESTINATION bin
    )
endif()
