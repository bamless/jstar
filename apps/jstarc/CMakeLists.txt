add_executable(jstarc main.c)

target_link_libraries(jstarc PRIVATE jstar common argparse)
target_include_directories(jstarc PRIVATE ${PROJECT_BINARY_DIR})
if(LTO)
    set_target_properties(jstarc PROPERTIES INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()

if(WIN32)
    target_sources(cli PRIVATE icon.rc)
    add_custom_command(TARGET jstarc POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_RUNTIME_DLLS:jstarc>        # all needed DLLs
        $<TARGET_FILE_DIR:jstarc>
      COMMAND_EXPAND_LISTS
    )
endif()

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
