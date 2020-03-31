include_directories(
	"${PROJECT_SOURCE_DIR}/jstar/include"
	"thirdparty/linenoise/include"
)

add_subdirectory("thirdparty/linenoise")

# setup relative rpath
if(APPLE)
    set(CMAKE_INSTALL_RPATH "@executable_path/../lib")
else()
    set(CMAKE_INSTALL_RPATH "$ORIGIN/../lib")
endif()

# add executable and libjstar dependency
add_executable(jstar src/cli.c)
target_link_libraries(jstar libjstar linenoise)