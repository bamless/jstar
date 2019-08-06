# math library
include_directories(
	"${PROJECT_SOURCE_DIR}/vm"
	"${PROJECT_SOURCE_DIR}/include"
	"${PROJECT_SOURCE_DIR}/thirdparty/linenoise/include"
)

# add executable and libjstar dependency
SET(CMAKE_INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/lib")
add_executable(jstar cli.c)
target_link_libraries(jstar libjstar linenoise)