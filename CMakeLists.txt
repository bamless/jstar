cmake_minimum_required(VERSION 3.9)
project(jstar)

set(JSTAR_VERSION_MAJOR 1)
set(JSTAR_VERSION_MINOR 8)
set(JSTAR_VERSION_PATCH 6)
set(JSTAR_VERSION ${JSTAR_VERSION_MAJOR}.${JSTAR_VERSION_MINOR}.${JSTAR_VERSION_PATCH})

set(CMAKE_C_STANDARD 99)

# General options
option(JSTAR_INSTALL        "Generate install targets" ON)
option(JSTAR_COMPUTED_GOTOS "Use computed gotos for VM eval loop" ON)
option(JSTAR_NAN_TAGGING    "Use NaN tagging technique to store the VM internal type" ON)
option(JSTAR_DBG_PRINT_EXEC "Trace the execution of the VM" OFF)
option(JSTAR_DBG_PRINT_GC   "Trace the execution of the garbage collector" OFF)
option(JSTAR_DBG_STRESS_GC  "Stress the garbage collector by calling it on every allocation" OFF)
option(JSTAR_INSTRUMENT     "Enable function instrumentation" OFF)

# Options for optional libraries
option(JSTAR_SYS   "Include the 'sys' module in the language" ON)
option(JSTAR_IO    "Include the 'io' module in the language" ON)
option(JSTAR_MATH  "Include the 'math' module in the language" ON)
option(JSTAR_DEBUG "Include the 'debug' module in the language" ON)
option(JSTAR_RE    "Include the 're' module in the language" ON)

# Setup config file
configure_file (
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/conf.h.in
    ${CMAKE_CURRENT_SOURCE_DIR}/include/jstar/conf.h
)

# Set default build type if not specified
get_property(IS_MULTI_CONFIG GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if (NOT IS_MULTI_CONFIG AND NOT CMAKE_BUILD_TYPE)
    message(STATUS "Setting build type to 'Release' as none was specified.")
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Choose the type of build." FORCE)
endif()

# Check for link time optimization support
if(NOT CMAKE_BUILD_TYPE STREQUAL "Debug" AND NOT CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
    include(CheckIPOSupported)
    check_ipo_supported(RESULT LTO)
    if(LTO)
        message(STATUS "J* link-time optimization enabled")
    endif()
endif()

# Set output directories
if(NOT CMAKE_RUNTIME_OUTPUT_DIRECTORY)
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
endif()
if(NOT CMAKE_LIBRARY_OUTPUT_DIRECTORY)
  set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
endif()
if(NOT CMAKE_ARCHIVE_OUTPUT_DIRECTORY)
  set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
endif()

# Set compiler flags
if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Wno-unused-parameter")
    set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -O3 -fomit-frame-pointer -fno-plt -s")
    set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} -s")
elseif(CMAKE_C_COMPILER_ID MATCHES "Clang" AND NOT MSVC)
    set(CMAKE_C_FLAGS "-Wall -Wextra -Wno-unused-parameter")
    set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -Qunused-arguments -O3 -fomit-frame-pointer -fno-plt -s")
    set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} -s")
elseif(MSVC)
    # Disable secure warnings for the useless _s variants of standard functions.
    # These shouldn't even exist in c99 but MSVC will complain about them because it's MSVC. 
    add_definitions(-D_CRT_SECURE_NO_WARNINGS)
    add_definitions(-DWIN32_LEAN_AND_MEAN)
    add_definitions(-DNOMINMAX)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /wd4244 /wd4267 /wd5105 /wd4116")
endif()

# Instrumentation
add_subdirectory(profile)

# J*, cli, compiler and external dependencies
add_subdirectory(src)
add_subdirectory(apps)
add_subdirectory(extern)

if(JSTAR_INSTALL)
    # Install export files
    install(EXPORT jstar-export
        FILE        JStarTargets.cmake
        NAMESPACE   jstar::
        DESTINATION lib/cmake/jstar
    )

    include(CMakePackageConfigHelpers)
    write_basic_package_version_file(
        ${CMAKE_CURRENT_BINARY_DIR}/JStarConfigVersion.cmake
        VERSION ${JSTAR_VERSION}
        COMPATIBILITY AnyNewerVersion
    )

    install(FILES
        ${CMAKE_CURRENT_SOURCE_DIR}/cmake/JStarConfig.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/JStarConfigVersion.cmake
        DESTINATION lib/cmake/jstar
    )

    # Install license files
    install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/LICENSE DESTINATION share/licenses/jstar)
    install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/extern/argparse/LICENSE DESTINATION share/licenses/jstar/argparse)
    install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/extern/cwalk/LICENSE.md DESTINATION share/licenses/jstar/cwalk)
    install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/extern/dirent/LICENSE DESTINATION share/licenses/jstar/dirent)
    install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/extern/replxx/LICENSE.md DESTINATION share/licenses/jstar/replxx)
    
    # On Windows install required runtime dlls alongside exe
    include(cmake/MinGWRuntime.cmake)
    include(InstallRequiredSystemLibraries)

    # Packaging support
    set(CPACK_PACKAGE_VENDOR "bamless")
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "J* - A Lightweight Scripting Language")
    set(CPACK_PACKAGE_EXECUTABLES jstar;jstar)
    set(CPACK_PACKAGE_VERSION ${JSTAR_VERSION})
    set(CPACK_PACKAGE_VERSION_MAJOR ${JSTAR_VERSION_MAJOR})
    set(CPACK_PACKAGE_VERSION_MINOR ${JSTAR_VERSION_MINOR})
    set(CPACK_PACKAGE_VERSION_PATCH ${JSTAR_VERSION_PATCH})
    set(CPACK_RESOURCE_FILE_LICENSE ${CMAKE_CURRENT_SOURCE_DIR}/LICENSE)
    set(CPACK_RESOURCE_FILE_README ${CMAKE_CURRENT_SOURCE_DIR}/README.md)
    set(CPACK_PACKAGE_INSTALL_DIRECTORY "jstar")
    include(CPack)
endif()
