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
    ${PROJECT_SOURCE_DIR}/include/jstar/parse/vector.h

    parse/ast.c
    parse/lex.c
    parse/parser.c

    lib/core/core.c
    lib/core/core.h
    lib/core/std.h
    lib/core/std.c
    lib/core/excs.h
    lib/core/excs.c
    lib/core/iter.h
    lib/core/iter.c
    lib/builtins.h
    lib/builtins.c

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
    hash_table.h
    value_hash_table.h
    value_hash_table.c
    int_hash_table.h
    int_hash_table.c
    import.c
    import.h
    jstar.c
    jstar_limits.h
    object.c
    object.h
    opcode.h
    opcode.c
    serialize.c
    serialize.h
    util.h
    value.c
    value.h
    vm.c
    vm.h
)

# J* standard library files
set(JSTAR_STDLIB
    lib/core/core.jsc
    lib/core/std.jsc
    lib/core/excs.jsc
    lib/core/iter.jsc
)

# Add optional module source files
if(JSTAR_SYS)
    list(APPEND JSTAR_SOURCES lib/sys.h lib/sys.c)
    list(APPEND JSTAR_STDLIB  lib/sys.jsc)
endif()
if(JSTAR_IO)
    list(APPEND JSTAR_SOURCES lib/io.h lib/io.c)
    list(APPEND JSTAR_STDLIB  lib/io.jsc)
endif()
if(JSTAR_MATH)
    list(APPEND JSTAR_SOURCES lib/math.h lib/math.c)
    list(APPEND JSTAR_STDLIB  lib/math.jsc)
endif()
if(JSTAR_DEBUG)
    list(APPEND JSTAR_SOURCES lib/debug.h lib/debug.c)
    list(APPEND JSTAR_STDLIB  lib/debug.jsc)
endif()
if(JSTAR_RE)
    list(APPEND JSTAR_SOURCES lib/re.h lib/re.c)
    list(APPEND JSTAR_STDLIB  lib/re.jsc)
endif()

# Generate J* sandard library source headers
set(JSTAR_STDLIB_HEADERS)
foreach(jsr ${JSTAR_STDLIB})
    list(APPEND JSTAR_STDLIB_HEADERS ${CMAKE_CURRENT_SOURCE_DIR}/${jsr}.inc)
    add_custom_command(
        OUTPUT  ${CMAKE_CURRENT_SOURCE_DIR}/${jsr}.inc
        COMMAND ${Python_EXECUTABLE} ${PROJECT_SOURCE_DIR}/scripts/bin2incl.py ${CMAKE_CURRENT_SOURCE_DIR}/${jsr} ${CMAKE_CURRENT_SOURCE_DIR}/${jsr}.inc
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${jsr}
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
        ${PROJECT_SOURCE_DIR}/profile
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
        ${PROJECT_SOURCE_DIR}/profile
        ${PROJECT_BINARY_DIR}
)
set_target_properties(jstar PROPERTIES
    PDB_NAME "jstar.dll"
    VERSION ${JSTAR_VERSION}
    SOVERSION ${JSTAR_VERSION_MAJOR}
    C_VISIBILITY_PRESET hidden
)

if(JSTAR_INSTRUMENT)
    target_link_libraries(jstar PRIVATE profile)
endif()

# Enable link-time optimization if supported
if(LTO)
    set_target_properties(jstar PROPERTIES INTERPROCEDURAL_OPTIMIZATION TRUE)
    set_target_properties(jstar_static PROPERTIES INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()

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
