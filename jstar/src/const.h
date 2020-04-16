#ifndef CONST_H
#define CONST_H

// Runtime constants
#define RECURSION_LIMIT 5000                       // Max recursion depth
#define FRAME_SZ        100                        // Starting frame size
#define STACK_SZ        FRAME_SZ*(MAX_LOCALS + 1)  // Stack size given frames
#define INIT_GC         (1024 * 1024 * 10)         // 10MiB - First GC collection point
#define HANDLER_MAX     10                         // Max number of try-excepts for a frame

// Compiler constants
#define MAX_TRY_DEPTH HANDLER_MAX  // Max depth of nested trys
#define MAX_LOCALS    UINT8_MAX    // At most 255 local vars per frame

// String constants
#define CTOR_STR    "new"
#define THIS_STR    "this"
#define ANON_PREFIX "anon@"

#define EXC_M_ERR        "__err"
#define EXC_M_STACKTRACE "__stacktrace"

#define PACKAGE_FILE "/__package__.jsr"

#ifdef __unix__
#    define DL_PREFIX "lib"
#    define DL_SUFFIX ".so"
#elif defined(__APPLE__) && defined(__MACH__)
#    define DL_PREFIX ""
#    define DL_SUFFIX ".dylib"
#elif defined(_WIN32)
#    define DL_PREFIX ""
#    define DL_SUFFIX ".dll"
#else
#    define DL_PREFIX ""
#    define DL_SUFFIX ""
#endif

#endif