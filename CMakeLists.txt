cmake_minimum_required(VERSION 2.6)
project(jstar)

# version
set(JSTAR_VERSION_MAJOR 1)
set(JSTAR_VERSION_MINOR 1)
set(JSTAR_VERSION_PATCH 1)

# set the c standard
set(CMAKE_C_STANDARD 99)

# options
option(JSTAR_INSTALL      "Generate install targets" ON)
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
	"${CMAKE_SOURCE_DIR}/jstar/include/jstarconf.h.in"
	"${CMAKE_SOURCE_DIR}/jstar/include/jstarconf.h"
)

# set default build type if not specified
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
	message(STATUS "Setting build type to 'Release' as none was specified.")
	set(CMAKE_BUILD_TYPE "Release")
endif()

# set output directories
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_BUILD_RPATH_USE_ORIGIN   TRUE)

# set compiler flags
if(CMAKE_COMPILER_IS_GNUCC)
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Wno-unused-parameter")
	set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -O3 -flto -fomit-frame-pointer -s")
	set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} -flto -s")
elseif(CMAKE_C_COMPILER_ID MATCHES "Clang" AND NOT MSVC)
	set(CMAKE_C_FLAGS "-Wall -Wextra -Wno-unused-parameter -Wno-unused-command-line-argument")
	set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -O3 -flto -fomit-frame-pointer -s")
	set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} -flto -s")
elseif(MSVC)
	add_definitions("-D_CRT_SECURE_NO_WARNINGS=1")
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /wd4244")
	set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} /Ox /GL")
	set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /LTCG")
	set(CMAKE_MODULE_LINKER_FLAGS "${CMAKE_MODULE_LINKER_FLAGS} /LTCG")
endif()

# required programs
find_package(PythonInterp 3 REQUIRED)

# subprojects
add_subdirectory("cli")
add_subdirectory("jstar")

# ---- install targets ----

if(JSTAR_INSTALL)
	# install all build products
	install(TARGETS jstar libjstar libjstar_static
		LIBRARY DESTINATION lib
		ARCHIVE DESTINATION lib
		RUNTIME DESTINATION bin
	)

	# install header files
	install(DIRECTORY
		"jstar/include/" 
		DESTINATION include
		FILES_MATCHING PATTERN "*.h"
	)
endif()