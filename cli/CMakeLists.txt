# math library
include_directories(
	"${PROJECT_SOURCE_DIR}/jstar/src"
	"${PROJECT_SOURCE_DIR}/include"
	"thirdparty/linenoise/include"
)

# add executable and libjstar dependency
SET(CMAKE_INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/lib")
add_executable(jstar src/cli.c)
target_link_libraries(jstar libjstar linenoise)