cmake_minimum_required(VERSION 2.6)
project(JStar)

# C version
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# version
set(JSTAR_VERSION_MAJOR 0)
set(JSTAR_VERSION_MINOR 5)
set(JSTAR_VERSION_PATCH 5)

# options
option(USE_COMPUTED_GOTOS "Use computed gotos for VM eval loop" ON)
option(NAN_TAGGING "Use NaN tagging technique to store the VM internal type" ON)
option(DBG_PRINT_EXEC "Trace the execution of the VM" OFF)
option(DBG_PRINT_GC "Trace the execution of the garbage collector" OFF)
option(DBG_STRESS_GC "Stress the garbage collector by calling it on every allocation" OFF)

# setup option.h
configure_file (
	"${PROJECT_SOURCE_DIR}/vm/jstarconf.h.in"
	"${PROJECT_SOURCE_DIR}/include/jstarconf.h"
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
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Wno-unused-parameter -Wno-unused-label")
	set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -O0 -g")
	set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -O3 -flto -fomit-frame-pointer -s")
elseif(${CMAKE_C_COMPILER_ID} STREQUAL "Clang")
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Wno-unused-parameter")
	set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -O0 -g")
	set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -O3 -fomit-frame-pointer -s -Qunused-arguments -Wno-unused-label -Wno-unused-command-line-argument")
elseif(MSVC)
	set(CMAKE_C_FLAGS "/wd4244")
	add_definitions("-D_CRT_SECURE_NO_WARNINGS=1")
	set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} /Ox /GL")
endif()

# required programs
find_package(PythonInterp 3 REQUIRED)

# Global include directory
include_directories(${PROJECT_BINARY_DIR})

# Subprojects
include("../cmake/linenoise.cmake")
add_subdirectory("${PROJECT_SOURCE_DIR}/cli")
add_subdirectory("${PROJECT_SOURCE_DIR}/vm")

# ---- install targets ----

#install libjstar along with jstar headers
install(TARGETS jstar libjstar libjstar_static
	LIBRARY DESTINATION "${CMAKE_INSTALL_PREFIX}/lib"
	RUNTIME DESTINATION "${CMAKE_INSTALL_PREFIX}/bin"
	PUBLIC_HEADER DESTINATION "${CMAKE_INSTALL_PREFIX}/include"
)
install(DIRECTORY 
	"${CMAKE_SOURCE_DIR}/vm/jsrparse" DESTINATION "${CMAKE_INSTALL_PREFIX}/include"
)