set(JSTAR_SOURCES
    ${PROJECT_SOURCE_DIR}/include/jstar/jstar.h
    ${PROJECT_SOURCE_DIR}/include/jstar/jstarconf.h
    ${PROJECT_SOURCE_DIR}/include/jstar/parse/ast.h
    ${PROJECT_SOURCE_DIR}/include/jstar/parse/lex.h
    ${PROJECT_SOURCE_DIR}/include/jstar/parse/parser.h
    ${PROJECT_SOURCE_DIR}/include/jstar/parse/token.def
    ${PROJECT_SOURCE_DIR}/include/jstar/parse/vector.h

    parse/ast.c
    parse/lex.c
    parse/parser.c
    parse/vector.c

    std/core.c
    std/core.h
    std/debug.c
    std/debug.h
    std/io.c
    std/io.h
    std/math.c
    std/math.h
    std/modules.c
    std/modules.h
    std/re.c
    std/re.h
    std/sys.c
    std/sys.h

    code.c
    code.h
    compiler.c
    compiler.h
    const.h
    disassemble.c
    disassemble.h
    dynload.c
    dynload.h
    gc.c
    gc.h
    hashtable.c
    hashtable.h
    import.c
    import.h
    jstar.c
    object.c
    object.h
    opcode.def
    opcode.h
    util.h
    value.c
    value.h
    vm.c
    vm.h
)

set(JSTAR_STDLIB
    std/core.jsr 
    std/debug.jsr
    std/io.jsr
    std/math.jsr
    std/re.jsr
    std/sys.jsr
)

if(USE_COMPUTED_GOTOS)
    if(${CMAKE_C_COMPILER_ID} STREQUAL "GNU")
        # disable crossjumping optimization on vm.c for an extra ~15% dispatch performance
        set_property(SOURCE "vm.c" PROPERTY COMPILE_FLAGS "-fno-crossjumping ")
    endif()
endif()

# Create commands to generate jstar sandard library source headers
set(JSTAR_STDLIB_HEADERS)
foreach(jsr ${JSTAR_STDLIB})
    list(APPEND JSTAR_STDLIB_HEADERS ${CMAKE_CURRENT_SOURCE_DIR}/${jsr}.h)
    add_custom_command(
        OUTPUT  ${CMAKE_CURRENT_SOURCE_DIR}/${jsr}.h
        COMMAND python ${PROJECT_SOURCE_DIR}/scripts/txt2incl.py ${CMAKE_CURRENT_SOURCE_DIR}/${jsr} ${CMAKE_CURRENT_SOURCE_DIR}/${jsr}.h
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${jsr}
    )
endforeach()

# set extra libraries that we need to link
set(EXTRA_LIBS)
if(UNIX)
    set(EXTRA_LIBS dl m)
endif()

# static library
add_library(jstar_static STATIC ${JSTAR_SOURCES} ${JSTAR_STDLIB_HEADERS} ${JSTAR_STDLIB})
target_compile_definitions(jstar_static PRIVATE JSTAR_STATIC)
target_link_libraries(jstar_static ${EXTRA_LIBS})
target_include_directories(jstar_static 
    PUBLIC
        $<INSTALL_INTERFACE:include>
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include/jstar>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)
set_target_properties(jstar_static PROPERTIES 
    OUTPUT_NAME "jstars"
    VERSION     ${JSTAR_VERSION}
)

#shared library
add_library(jstar SHARED ${JSTAR_SOURCES} ${JSTAR_STDLIB_HEADERS} ${JSTAR_STDLIB})
target_link_libraries(jstar ${EXTRA_LIBS})
target_include_directories(jstar 
    PUBLIC
        $<INSTALL_INTERFACE:include>
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include/jstar>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)
set_target_properties(jstar PROPERTIES
    PDB_NAME "jstar.dll"
    VERSION ${JSTAR_VERSION}
    SOVERSION ${JSTAR_VERSION_MAJOR}
    C_VISIBILITY_PRESET hidden
)

# Enable link-time optimization if supported
if(LTO)
    set_target_properties(jstar PROPERTIES INTERPROCEDURAL_OPTIMIZATION TRUE)
    set_target_properties(jstar_static PROPERTIES INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()

# Install target
if(JSTAR_INSTALL)
    # Install J* library
    install(TARGETS jstar jstar_static
        EXPORT  jstar-export
        RUNTIME DESTINATION bin
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib
    )

    # Install header files
    install(DIRECTORY
        ${PROJECT_SOURCE_DIR}/include/ 
        DESTINATION include
        PATTERN "*.h.in" EXCLUDE
    )
endif()
