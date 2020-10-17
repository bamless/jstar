FILE(GLOB_RECURSE SOURCES src/*.c)
FILE(GLOB_RECURSE JSTAR_SOURCES src/*.jsr)

if(USE_COMPUTED_GOTOS)
    if(${CMAKE_C_COMPILER_ID} STREQUAL "GNU")
        # disable crossjumping optimization on vm.c for an extra ~15% dispatch performance
        set_property(SOURCE "src/vm.c" PROPERTY COMPILE_FLAGS "-fno-crossjumping ")
    endif()
endif()

# create jstar source headers names
set(JSTAR_HEADERS)
foreach(jsr ${JSTAR_SOURCES})
    list(APPEND JSTAR_HEADERS "${jsr}.h")
endforeach()

# create commands to generate jstar source headers
foreach(jsr ${JSTAR_SOURCES})
    add_custom_command(
        OUTPUT  "${jsr}.h"
        COMMAND python "${PROJECT_SOURCE_DIR}/util/txt2incl.py" ${jsr} "${jsr}.h"
        DEPENDS ${jsr}
    )
endforeach()

# set extra libraries that we need to link
set(EXTRA_LIBS)
if(UNIX)
    set(EXTRA_LIBS dl m)
endif()

# static library
add_library(libjstar_static STATIC ${SOURCES} ${JSTAR_HEADERS})
target_compile_definitions(libjstar_static PRIVATE JSTAR_STATIC)
target_link_libraries(libjstar_static ${EXTRA_LIBS})
target_include_directories(libjstar_static 
    PUBLIC
        $<INSTALL_INTERFACE:include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include/jstar>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)
set_target_properties(libjstar_static PROPERTIES 
    OUTPUT_NAME "jstars"
    VERSION     ${JSTAR_VERSION}
)

#shared library
add_library(libjstar SHARED ${SOURCES} ${JSTAR_HEADERS})
target_link_libraries(libjstar ${EXTRA_LIBS})
target_include_directories(libjstar 
    PUBLIC
        $<INSTALL_INTERFACE:include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include/jstar>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)
set_target_properties(libjstar PROPERTIES
    OUTPUT_NAME "jstar"
    PDB_NAME "libjstar"
    VERSION ${JSTAR_VERSION}
    SOVERSION ${JSTAR_VERSION_MAJOR}
    C_VISIBILITY_PRESET hidden
)

# Enable link-time optimization if supported
if(LTO)
    set_target_properties(libjstar PROPERTIES INTERPROCEDURAL_OPTIMIZATION TRUE)
    set_target_properties(libjstar_static PROPERTIES INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()
