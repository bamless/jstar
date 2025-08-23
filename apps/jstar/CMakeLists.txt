# Cli app
add_executable(cli
    completion.h
    completion.c
    console_print.h
    console_print.c
    dynload.h
    highlighter.h
    highlighter.c
    import.h
    import.c
    main.c
)

if(WIN32)
    target_sources(cli PRIVATE icon.rc)
endif()

set(EXTRA_LIBS)
if(UNIX)
    set(EXTRA_LIBS dl)
endif()

target_link_libraries(cli PRIVATE jstar common replxx argparse ${EXTRA_LIBS})
target_include_directories(cli PRIVATE ${PROJECT_BINARY_DIR})
set_target_properties(cli PROPERTIES OUTPUT_NAME "jstar")

# Enable link-time optimization if supported
if(LTO)
    set_target_properties(cli PROPERTIES INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()

# Install target
if(JSTAR_INSTALL)
    include(GNUInstallDirs)

    # Setup relative rpath on unix and macos
    if(APPLE)
        set_target_properties(cli PROPERTIES INSTALL_RPATH "@executable_path/../${CMAKE_INSTALL_LIBDIR}")
    elseif(UNIX)
        set_target_properties(cli PROPERTIES INSTALL_RPATH "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}")
    endif()
    
    install(TARGETS cli
        EXPORT jstar-export
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
endif()
