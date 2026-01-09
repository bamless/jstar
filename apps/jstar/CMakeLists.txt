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

target_link_libraries(cli PRIVATE jstar common replxx argparse ${CMAKE_DL_LIBS})
target_include_directories(cli PRIVATE ${PROJECT_BINARY_DIR})
set_target_properties(cli PROPERTIES OUTPUT_NAME "jstar")
if(LTO)
    set_target_properties(cli PROPERTIES INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()

if(WIN32)
    target_sources(cli PRIVATE icon.rc)
    add_custom_command(TARGET cli POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_RUNTIME_DLLS:cli>        # all needed DLLs
        $<TARGET_FILE_DIR:cli>
      COMMAND_EXPAND_LISTS
    )
endif()

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
