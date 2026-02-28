# =============================================================================
# WASM build for the documentation site  (Emscripten only)
# =============================================================================
#
# Usage from the repo root:
#
#   # 1. Build the native host tool first (bin2incl generates .jsc.inc headers)
#   cmake -S . -B build-native -DJSTAR_INSTALL=OFF
#   cmake --build build-native --target bin2incl
#
#   # 2. Cross-compile to WASM, pointing at the native bin2incl
#   emcmake cmake -S . -B build-wasm -DCMAKE_BUILD_TYPE=Release -DJSTAR_INSTALL=OFF \
#       -DJSTAR_BIN2INCL=build-native/apps/bin2incl/bin2incl
#   cmake --build build-wasm --target docs
#
# The `docs` target compiles docs/jstar.c against the static jstar library and
# copies the resulting jstar.js / jstar.wasm pair into docs/assets/js/ so that
# Jekyll picks them up when building the site.

add_executable(jstar-docs ${CMAKE_CURRENT_SOURCE_DIR}/jstar.c)
target_link_libraries(jstar-docs PRIVATE jstar_static)

# Use "jstar" as the output name so Emscripten produces jstar.js + jstar.wasm.
set_target_properties(jstar-docs PROPERTIES OUTPUT_NAME "jstar")

# Emscripten link settings:
#   EXPORTED_FUNCTIONS   — keep _jstar_run reachable from JavaScript
#   EXPORTED_RUNTIME_METHODS — expose cwrap() so jstar-api.js can wrap it
#   NO_EXIT_RUNTIME      — keep the runtime alive after jstar_run() returns
#   ALLOW_MEMORY_GROWTH  — let the heap expand for large user programs
target_link_options(jstar-docs PRIVATE
    "SHELL:-sEXPORTED_FUNCTIONS=['_jstar_run']"
    "SHELL:-sEXPORTED_RUNTIME_METHODS=['cwrap']"
    -sNO_EXIT_RUNTIME=1
    -sALLOW_MEMORY_GROWTH=1
)

# After a successful build, copy the .js loader and .wasm binary into the docs
# assets directory where Jekyll expects them.
set(ASSETS_JS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/assets/js)

add_custom_command(TARGET jstar-docs POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
        $<TARGET_FILE:jstar-docs>
        ${ASSETS_JS_DIR}/jstar.js
    COMMAND ${CMAKE_COMMAND} -E copy
        $<TARGET_FILE_DIR:jstar-docs>/jstar.wasm
        ${ASSETS_JS_DIR}/jstar.wasm
    COMMENT "Copying WASM artifacts to docs/assets/js/"
)

# Named alias so callers can run: cmake --build <dir> --target docs
add_custom_target(docs DEPENDS jstar-docs)