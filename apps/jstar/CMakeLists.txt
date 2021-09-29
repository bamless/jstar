# Cli app
add_executable(cli cli.c console_print.h console_print.c highlighter.h highlighter.c hints.h hints.c)
if(WIN32)
    target_sources(cli PRIVATE icon.rc)
endif()

target_link_libraries(cli PRIVATE jstar replxx argparse cwalk)
target_include_directories(cli PRIVATE ${PROJECT_BINARY_DIR} ${PROJECT_SOURCE_DIR}/profile)
set_target_properties(cli PROPERTIES OUTPUT_NAME "jstar")

if(JSTAR_INSTRUMENT)
    target_link_libraries(cli PRIVATE profile)
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
