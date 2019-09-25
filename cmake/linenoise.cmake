include(${CMAKE_ROOT}/Modules/ExternalProject.cmake)

set(LIB ${CMAKE_STATIC_LIBRARY_PREFIX})
set(A   ${CMAKE_STATIC_LIBRARY_SUFFIX})

# Linenoise
ExternalProject_Add(thirdparty_linenoise
	SOURCE_DIR 	     "${CMAKE_SOURCE_DIR}/thirdparty/linenoise"
	BINARY_DIR 	     "${CMAKE_BINARY_DIR}/thirdparty"
	BUILD_BYPRODUCTS "${CMAKE_BINARY_DIR}/thirdparty/${LIB}linenoise${A}"
	BUILD_COMMAND    "${CMAKE_COMMAND}" --build . --config $<CONFIG> --target linenoise
	CMAKE_ARGS       -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER} -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER} -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
	BUILD_ALWAYS     1
	INSTALL_COMMAND  ""
)

add_library(linenoise STATIC IMPORTED)
if(MSVC)
	set_target_properties(linenoise PROPERTIES
		IMPORTED_LOCATION_DEBUG "${CMAKE_BINARY_DIR}/thirdparty/Debug/${LIB}linenoise${A}"
		IMPORTED_LOCATION_RELEASE "${CMAKE_BINARY_DIR}/thirdparty/Release/${LIB}linenoise${A}"
	)
else()
	set_target_properties(linenoise PROPERTIES 
		IMPORTED_LOCATION "${CMAKE_BINARY_DIR}/thirdparty/${LIB}linenoise${A}"
		IMPORTED_LINK_INTERFACE_LANGUAGES "CXX"
	)
endif()
add_dependencies(linenoise thirdparty_linenoise)