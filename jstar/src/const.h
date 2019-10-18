#ifndef CONST_H
#define CONST_H

// Runtime and compiler constants
#define RECURSION_LIMIT 5000                       // After this many calls, StackOverflowException will be raised
#define FRAME_SZ        1000                       // Starting frame size
#define STACK_SZ        FRAME_SZ * (UINT8_MAX + 1) // We have at most UINT8_MAX+1 local var per frame
#define INIT_GC         (1024 * 1024 * 10)         // 10MiB

#define HANDLER_MAX     10                         // Max number of nested try/except/ensure
#define MAX_TRY_DEPTH   HANDLER_MAX                // Max depth of nested trys
#define MAX_LOCALS      UINT8_MAX

// String constants
#define CTOR_STR "new"
#define THIS_STR "this"
#define JSTARPATH "JSTARPATH"

// Import constants
#define PACKAGE_FILE "/__package__.jsr"

#ifdef __unix__
    #define DL_PREFIX "lib"
    #define DL_SUFFIX ".so"
#elif defined (__APPLE__) && defined (__MACH__)
    #define DL_PREFIX ""
    #define DL_SUFFIX ".dylib"
#elif defined(_WIN32)
    #define DL_PREFIX ""
    #define DL_SUFFIX ".dll"
#else
    #define DL_PREFIX ""
    #define DL_SUFFIX ""
#endif

#endif