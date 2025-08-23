# -----------------------------------------------------------------------------
# Sources
# -----------------------------------------------------------------------------

set(JSTAR_SOURCES
    ${PROJECT_SOURCE_DIR}/include/jstar/jstar.h
    ${PROJECT_SOURCE_DIR}/include/jstar/buffer.h
    ${PROJECT_SOURCE_DIR}/include/jstar/conf.h
    ${PROJECT_SOURCE_DIR}/include/jstar/parse/ast.h
    ${PROJECT_SOURCE_DIR}/include/jstar/parse/lex.h
    ${PROJECT_SOURCE_DIR}/include/jstar/parse/parser.h

    parse/ast.c
    parse/lex.c
    parse/parser.c

    builtins/core/core.c
    builtins/core/core.h
    builtins/core/std.h
    builtins/core/std.c
    builtins/core/excs.h
    builtins/core/excs.c
    builtins/core/iter.h
    builtins/core/iter.c
    builtins/builtins.h
    builtins/builtins.c

    array.h
    buffer.c
    code.c
    code.h
    compiler.c
    compiler.h
    disassemble.c
    disassemble.h
    endianness.h
    gc.c
    gc.h
    hashtable.h
    import.c
    import.h
    int_hashtable.h
    int_hashtable.c
    jstar.c
    jstar_limits.h
    object.c
    object.h
    opcode.h
    opcode.c
    profile.h
    profile.c
    serialize.c
    serialize.h
    util.h
    value.c
    value.h
    value_hashtable.h
    value_hashtable.c
    vm.c
    vm.h
)

# J* standard library files
set(JSTAR_STDLIB
    builtins/core/core.jsc
    builtins/core/std.jsc
    builtins/core/excs.jsc
    builtins/core/iter.jsc
)

# Add optional module source files
if(JSTAR_SYS)
    list(APPEND JSTAR_SOURCES builtins/sys.h builtins/sys.c)
    list(APPEND JSTAR_STDLIB  builtins/sys.jsc)
endif()
if(JSTAR_IO)
    list(APPEND JSTAR_SOURCES builtins/io.h builtins/io.c)
    list(APPEND JSTAR_STDLIB  builtins/io.jsc)
endif()
if(JSTAR_MATH)
    list(APPEND JSTAR_SOURCES builtins/math.h builtins/math.c)
    list(APPEND JSTAR_STDLIB  builtins/math.jsc)
endif()
if(JSTAR_DEBUG)
    list(APPEND JSTAR_SOURCES builtins/debug.h builtins/debug.c)
    list(APPEND JSTAR_STDLIB  builtins/debug.jsc)
endif()
if(JSTAR_RE)
    list(APPEND JSTAR_SOURCES builtins/re.h builtins/re.c)
    list(APPEND JSTAR_STDLIB  builtins/re.jsc)
endif()

# Generate J* sandard library source headers
set(JSTAR_STDLIB_HEADERS)
foreach(jsr ${JSTAR_STDLIB})
    list(APPEND JSTAR_STDLIB_HEADERS ${CMAKE_CURRENT_SOURCE_DIR}/${jsr}.inc)
    add_custom_command(
        OUTPUT  ${CMAKE_CURRENT_SOURCE_DIR}/${jsr}.inc
        COMMAND bin2incl ARGS ${CMAKE_CURRENT_SOURCE_DIR}/${jsr} ${CMAKE_CURRENT_SOURCE_DIR}/${jsr}.inc
        DEPENDS bin2incl ${CMAKE_CURRENT_SOURCE_DIR}/${jsr}
    )
endforeach()

# -----------------------------------------------------------------------------
# Prepare extra libraries
# -----------------------------------------------------------------------------

# set extra libraries that we need to link
set(EXTRA_LIBS)
if(UNIX)
    set(EXTRA_LIBS m)
endif()

if(JSTAR_COMPUTED_GOTOS)
    if(${CMAKE_C_COMPILER_ID} STREQUAL "GNU")
        # disable crossjumping optimization on vm.c for an extra ~15% dispatch performance
        set_property(SOURCE vm.c PROPERTY COMPILE_FLAGS -fno-crossjumping)
    endif()
endif()

# -----------------------------------------------------------------------------
# Library targets
# -----------------------------------------------------------------------------

# static library
add_library(jstar_static STATIC ${JSTAR_SOURCES} ${JSTAR_STDLIB_HEADERS} ${JSTAR_STDLIB})
target_compile_definitions(jstar_static PUBLIC JSTAR_STATIC)
target_link_libraries(jstar_static PUBLIC ${EXTRA_LIBS})
target_include_directories(jstar_static
    PUBLIC
        $<INSTALL_INTERFACE:include>
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${PROJECT_SOURCE_DIR}/include/jstar
        ${PROJECT_BINARY_DIR}
)
if(NOT WIN32)
    set_target_properties(jstar_static PROPERTIES
        OUTPUT_NAME "jstar"
        VERSION     ${JSTAR_VERSION}
    )
endif()

#shared library
add_library(jstar SHARED ${JSTAR_SOURCES} ${JSTAR_STDLIB_HEADERS} ${JSTAR_STDLIB})
target_link_libraries(jstar PUBLIC ${EXTRA_LIBS})
target_include_directories(jstar
    PUBLIC
        $<INSTALL_INTERFACE:include>
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${PROJECT_SOURCE_DIR}/include/jstar
        ${PROJECT_BINARY_DIR}
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

add_dependencies(jstar        bin2incl)
add_dependencies(jstar_static bin2incl)

# -----------------------------------------------------------------------------
# Installation
# -----------------------------------------------------------------------------

# Install target
if(JSTAR_INSTALL)
    include(GNUInstallDirs)

    # Install J* library
    install(TARGETS jstar jstar_static
        EXPORT  jstar-export
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    )

    # Install header files
    install(DIRECTORY
        ${PROJECT_SOURCE_DIR}/include/
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        PATTERN "*.h.in" EXCLUDE
    )

    # Configure and install pkg-config file
    configure_file(
        ${PROJECT_SOURCE_DIR}/cmake/jstar.pc.in
        ${CMAKE_BINARY_DIR}/jstar.pc
        @ONLY
    )
    install(
        FILES ${CMAKE_BINARY_DIR}/jstar.pc
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig
    )
endif()
