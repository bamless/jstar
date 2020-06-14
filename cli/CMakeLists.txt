include_directories(
	"${PROJECT_SOURCE_DIR}/jstar/include"
    "thirdparty/linenoise/include"
    "thirdparty/argparse"
)

add_subdirectory("thirdparty/linenoise")
add_subdirectory("thirdparty/argparse")

# setup relative rpath on unix and macos
if(APPLE)
    set(CMAKE_INSTALL_RPATH "@executable_path/../lib")
else()
    set(CMAKE_INSTALL_RPATH "$ORIGIN/../lib")
endif()

# add executable and libjstar dependency
add_executable(jstar src/cli.c)
target_link_libraries(jstar libjstar linenoise argparse)

# Enable link-time optimization if supported
if(LTOAvailable)
	set_property(TARGET jstar PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()

# add icon to exe on windows
if(WIN32)
    target_sources(jstar PRIVATE "${CMAKE_SOURCE_DIR}/util/icon.rc")
endif()