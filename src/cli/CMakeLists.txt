# math library
if(${CMAKE_CXX_COMPILER_ID} STREQUAL "GNU" OR ${CMAKE_CXX_COMPILER_ID} STREQUAL "Clang")
	set(EXTRA_LIBS ${EXTRA_LIBS} m)
endif()

include_directories(
	"${PROJECT_SOURCE_DIR}/vm"
	"${PROJECT_SOURCE_DIR}/include"
	"${PROJECT_SOURCE_DIR}/thirdparty/linenoise-ng/include"
)

# add executable and libblang dependency
SET(CMAKE_INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/lib")
add_executable(blang cli.c)
target_link_libraries(blang libblang linenoise ${EXTRA_LIBS})

# install
install(TARGETS blang DESTINATION "${CMAKE_INSTALL_PREFIX}/bin")