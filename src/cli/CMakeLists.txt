# math library
if(${CMAKE_CXX_COMPILER_ID} STREQUAL "GNU" OR ${CMAKE_CXX_COMPILER_ID} STREQUAL "Clang")
	set(EXTRA_LIBS ${EXTRA_LIBS} m)
endif()

include_directories("${PROJECT_SOURCE_DIR}/parse" "${PROJECT_SOURCE_DIR}/include" "${PROJECT_SOURCE_DIR}/thirdparty")

# add executable and libblang dependency
add_executable(blang cli.c)
target_link_libraries(blang libblang blparse linenoise ${EXTRA_LIBS})

# install
install(TARGETS blang DESTINATION "${CMAKE_INSTALL_PREFIX}/bin")
