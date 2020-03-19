cmake_minimum_required(VERSION 2.6)
project(JStar)

# version
set(JSTAR_VERSION_MAJOR 1)
set(JSTAR_VERSION_MINOR 0)
set(JSTAR_VERSION_PATCH 0)

# set the c standard
set(CMAKE_C_STANDARD 99)

# options
option(USE_COMPUTED_GOTOS "Use computed gotos for VM eval loop" ON)
option(NAN_TAGGING        "Use NaN tagging technique to store the VM internal type" ON)
option(DBG_PRINT_EXEC     "Trace the execution of the VM" OFF)
option(DBG_PRINT_GC       "Trace the execution of the garbage collector" OFF)
option(DBG_STRESS_GC      "Stress the garbage collector by calling it on every allocation" OFF)

option(JSTAR_IO    "Include the 'io' module in the language" ON)
option(JSTAR_MATH  "Include the 'math' module in the language" ON)
option(JSTAR_DEBUG "Include the 'debug' module in the language" ON)
option(JSTAR_RE    "Include the 're' module in the language" ON)

# setup option.h
configure_file (
	"${PROJECT_SOURCE_DIR}/include/jstarconf.h.in"
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
if(CMAKE_COMPILER_IS_GNUCC)
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Wno-unused-parameter -Wno-unused-label")
	set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -O3 -flto -fomit-frame-pointer")
	set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} -flto -s")
elseif(CMAKE_C_COMPILER_ID MATCHES "Clang" AND NOT MSVC)
	set(CMAKE_C_FLAGS "-Wall -Wextra -Wno-unused-parameter -Wno-unused-label -Wno-typedef-redefinition")
	set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -O3 -flto -fomit-frame-pointer")
	set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} -flto -s")
elseif(MSVC)
	add_definitions("-D_CRT_SECURE_NO_WARNINGS=1")
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /wd4244")
	set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} /Ox /GL")
endif()

# required programs
find_package(PythonInterp 3 REQUIRED)

# Global include directory
include_directories(${PROJECT_BINARY_DIR})

# Subprojects
add_subdirectory("cli")
add_subdirectory("jstar")

# ---- install targets ----

#install libjstar along with jstar headers
install(TARGETS jstar libjstar libjstar_static
	LIBRARY DESTINATION "${CMAKE_INSTALL_PREFIX}/lib"
	RUNTIME DESTINATION "${CMAKE_INSTALL_PREFIX}/bin"
	PUBLIC_HEADER DESTINATION "${CMAKE_INSTALL_PREFIX}/include"
)
install(DIRECTORY 
	"${CMAKE_SOURCE_DIR}/jstar/src/jsrparse" 
	DESTINATION "${CMAKE_INSTALL_PREFIX}/include"
	FILES_MATCHING
	PATTERN "*.h"
)
