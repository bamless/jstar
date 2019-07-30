cmake_minimum_required(VERSION 2.6)
project(Blang)

# version
set(BLANG_VERSION_MAJOR 0)
set(BLANG_VERSION_MINOR 3)
set(BLANG_VERSION_PATCH 11)

# options
option(USE_COMPUTED_GOTOS "Use computed gotos for VM eval loop" ON)
option(NAN_TAGGING "Use NaN tagging technique to store the VM internal type" ON)
option(DBG_PRINT_EXEC "Trace the execution of the VM" OFF)
option(DBG_PRINT_GC "Trace the execution of the garbage collector" OFF)
option(DBG_STRESS_GC "Stress the garbage collector by calling it on every allocation" OFF)

# setup option.h
configure_file (
	"${PROJECT_SOURCE_DIR}/vm/blconf.h.in"
	"${PROJECT_SOURCE_DIR}/include/blconf.h"
)

# set output directories
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# set build type and  compilation options
set(default_build_type "Release")
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
	message(STATUS "Setting build type to '${default_build_type}' as none was specified.")
	set(CMAKE_BUILD_TYPE "${default_build_type}" CACHE STRING "Choose the type of build." FORCE)
endif()

# set compiler flags
if(${CMAKE_C_COMPILER_ID} STREQUAL "GNU")
	set(CMAKE_C_FLAGS "-Wall -Wextra -std=gnu11 -Wno-unused-parameter -Wno-unused-label")
	set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -O0 -g")
	set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -Ofast -flto -fomit-frame-pointer -s")
elseif(${CMAKE_C_COMPILER_ID} STREQUAL "Clang")
	set(CMAKE_C_FLAGS "-Wall -Wextra -std=gnu11 -Wno-unused-parameter")
	set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -O0 -g")
	set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -Ofast -fomit-frame-pointer -s -Qunused-arguments -Wno-unused-label -Wno-unused-command-line-argument")
elseif(MSVC)
	set(CMAKE_C_FLAGS "/wd4244")
	add_definitions("-D_CRT_SECURE_NO_WARNINGS=1")
	set(CMAKE_C_FLAGS_RELEASE "/Ox /GL")
endif()

# required programs
find_package(PythonInterp 3 REQUIRED)

# Subprojects

# setup thirdparty projects as external
include(${CMAKE_ROOT}/Modules/ExternalProject.cmake)

set(LIB ${CMAKE_STATIC_LIBRARY_PREFIX})
set(A   ${CMAKE_STATIC_LIBRARY_SUFFIX})

# Linenoise
ExternalProject_Add(thirdparty_linenoise
	SOURCE_DIR 	     "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/linenoise"
	BINARY_DIR 	     "${CMAKE_BINARY_DIR}/thirdparty"
	BUILD_BYPRODUCTS "${CMAKE_BINARY_DIR}/thirdparty/${LIB}linenoise${A}"
	BUILD_COMMAND    "${CMAKE_COMMAND}" --build . --config $<CONFIG> --target linenoise
	CMAKE_ARGS       -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER} -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER} -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
	BUILD_ALWAYS     1
	DOWNLOAD_COMMAND ""
	UPDATE_COMMAND   ""
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

# Subprojects
include_directories(${PROJECT_BINARY_DIR})
add_subdirectory("${PROJECT_SOURCE_DIR}/cli")
add_subdirectory("${PROJECT_SOURCE_DIR}/vm")

# ---- install target ----

#install libblang along with blang headers
install(TARGETS blang libblang libblang_static
	LIBRARY DESTINATION "${CMAKE_INSTALL_PREFIX}/lib"
	RUNTIME DESTINATION "${CMAKE_INSTALL_PREFIX}/bin"
	PUBLIC_HEADER DESTINATION "${CMAKE_INSTALL_PREFIX}/include"
)
install(DIRECTORY 
	"${CMAKE_SOURCE_DIR}/vm/blparse" DESTINATION "${CMAKE_INSTALL_PREFIX}/include"
)