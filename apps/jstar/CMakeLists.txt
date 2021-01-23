# Cli app
add_executable(cli cli.c colorio.h)
target_link_libraries(cli PRIVATE jstar linenoise argparse)
set_target_properties(cli PROPERTIES OUTPUT_NAME "jstar")

# Add platform specific sources
if(UNIX)
    target_sources(cli PRIVATE colorio_posix.c)
elseif(WIN32)
    target_sources(cli PRIVATE colorio_none.c icon.rc)
else()
    target_sources(cli PRIVATE colorio_none.c)
endif()

# Enable link-time optimization if supported
if(LTO)
    set_target_properties(cli PROPERTIES INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()

# Install target
if(JSTAR_INSTALL)
    # Setup relative rpath on unix and macos
    if(APPLE)
        set_target_properties(cli PROPERTIES INSTALL_RPATH "@executable_path/../lib")
    elseif(UNIX)
        set_target_properties(cli PROPERTIES INSTALL_RPATH "$ORIGIN/../lib")
    endif()
    
    install(TARGETS cli
        EXPORT jstar-export
        RUNTIME DESTINATION bin
    )
endif()
