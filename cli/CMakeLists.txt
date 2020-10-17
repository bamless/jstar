add_subdirectory("thirdparty/linenoise")
add_subdirectory("thirdparty/argparse")

# setup relative rpath on unix and macos
if(APPLE)
    set(CMAKE_INSTALL_RPATH "@executable_path/../lib")
else()
    set(CMAKE_INSTALL_RPATH "$ORIGIN/../lib")
endif()

# jstar executable
add_executable(jstar src/cli.c)
target_link_libraries(jstar libjstar linenoise argparse)
target_include_directories(jstar
    PRIVATE
        ${PROJECT_SOURCE_DIR}/jstar/include
        ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/linenoise/include
        ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/argparse
)

# Enable link-time optimization if supported
if(LTO)
    set_target_properties(jstar PROPERTIES INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()

# add icon to exe on windows
if(WIN32)
    target_sources(jstar PRIVATE "${PROJECT_SOURCE_DIR}/util/icon.rc")
endif()