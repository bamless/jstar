# find third party libraries
find_library(LIB_READLINE readline)

# math library
if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU" OR "${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
	set(EXTRA_LIBS ${EXTRA_LIBS} m)
endif()

# add executable and libblang dependency
add_executable(blang cli.c)
target_link_libraries(blang libblang ${LIB_READLINE} ${EXTRA_LIBS})
