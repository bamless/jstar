#ifndef CONST_H
#define CONST_H

#include <stdint.h>

// -----------------------------------------------------------------------------
// RUNTIME CONSTANTS
// -----------------------------------------------------------------------------

#define RECURSION_LIMIT 100000                         // Max recursion depth
#define FRAME_SZ        100                            // Default starting frame size
#define STACK_SZ        (FRAME_SZ) * (MAX_LOCALS + 1)  // Deafult starting stack size
#define INIT_GC         (1024 * 1024 * 20)             // 20MiB - First GC collection point
#define HEAP_GROW_RATE  2                              // The heap growing rate
#define HANDLER_MAX     6                              // Max number of try-excepts for a frame
#define SUPER_SLOT      0                              // Constant holding the method's super-class
#define LIST_DEF_CAP    8                              // Starting capacity of a List with at least 1 element
#define LIST_GROW_RATE  2                              // List growing rate
#define JSR_BUF_DEF_CAP 16                             // Default capacity of a JStarBuffer

// -----------------------------------------------------------------------------
// COMPILER CONSTANTS
// -----------------------------------------------------------------------------

#define MAX_TRY_DEPTH   HANDLER_MAX  // Max depth of nested trys
#define MAX_LOCALS      UINT8_MAX    // At most 255 local vars per frame
#define MAX_INLINE_ARGS 10           // Max number of inline arguments for function call

// -----------------------------------------------------------------------------
// STRING CONSTANTS
// -----------------------------------------------------------------------------

#define CTOR_STR   "new"
#define THIS_STR   "this"
#define ARGV_STR   "argv"
#define ANON_STR   "anon:"
#define IMPORT_STR "importPaths"

#define EXC_ERR   "_err"
#define EXC_CAUSE "_cause"
#define EXC_TRACE "_stacktrace"

#define MOD_NAME "__name__"
#define MOD_PATH "__path__"
#define MOD_THIS "__this__"

#define PACKAGE_FILE "__package__"
#define JSR_EXT      ".jsr"
#define JSC_EXT      ".jsc"

#if defined(JSTAR_WINDOWS)
    #define DL_PREFIX ""
    #define DL_SUFFIX ".dll"
#elif defined(JSTAR_MACOS) || defined(JSTAR_IOS)
    #define DL_PREFIX ""
    #define DL_SUFFIX ".dylib"
#elif defined(JSTAR_POSIX)
    #define DL_PREFIX "lib"
    #define DL_SUFFIX ".so"
#else
    #define DL_PREFIX ""
    #define DL_SUFFIX ""
#endif

#endif
