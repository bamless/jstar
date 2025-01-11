find_package(Git QUIET)
if(GIT_FOUND AND EXISTS "${PROJECT_SOURCE_DIR}/.git")
    # Update submodules as needed
    option(GIT_SUBMODULE "Check submodules during build" ON)
    if(GIT_SUBMODULE)
        message(STATUS "Submodule update")
        execute_process(COMMAND ${GIT_EXECUTABLE} submodule update --init --recursive
                        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                        RESULT_VARIABLE GIT_SUBMOD_RESULT)
        if(NOT GIT_SUBMOD_RESULT EQUAL "0")
            message(FATAL_ERROR "git submodule update --init failed with ${GIT_SUBMOD_RESULT}, please checkout submodules")
        endif()
    endif()
endif()

set(IGNORE_WARNINGS TRUE)
add_subdirectory(argparse EXCLUDE_FROM_ALL)
add_subdirectory(cwalk    EXCLUDE_FROM_ALL)
add_subdirectory(dirent   EXCLUDE_FROM_ALL)

set(REPLXX_BUILD_EXAMPLES OFF CACHE BOOL "Build the examples"      FORCE)
set(REPLXX_BUILD_PACKAGE  OFF CACHE BOOL "Generate package target" FORCE)
add_subdirectory(replxx   EXCLUDE_FROM_ALL)
