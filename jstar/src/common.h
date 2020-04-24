#ifndef COMMON_H
#define COMMON_H

#include <limits.h>
#include <stddef.h>
#include <stdint.h>

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

#define EXC_M_ERR        "_err"
#define EXC_M_STACKTRACE "_stacktrace"

#define PACKAGE_FILE "/__package__.jsr"

#ifdef __unix__
    #define DL_PREFIX "lib"
    #define DL_SUFFIX ".so"
#elif defined(__APPLE__) && defined(__MACH__)
    #define DL_PREFIX ""
    #define DL_SUFFIX ".dylib"
#elif defined(_WIN32)
    #define DL_PREFIX ""
    #define DL_SUFFIX ".dll"
#else
    #define DL_PREFIX ""
    #define DL_SUFFIX ""
#endif

// Macros for defining an enum with associated string names using X macros
#define DEFINE_ENUM(NAME, ENUMX) typedef enum NAME { ENUMX(ENUM_ENTRY) } NAME
#define ENUM_ENTRY(ENTRY)        ENTRY,

#define DECLARE_ENUM_STRINGS(NAME)       extern const char* CONCATOK(NAME, Name)[]
#define DEFINE_ENUM_STRINGS(NAME, ENUMX) const char* CONCATOK(NAME, Name)[] = {ENUMX(STRINGIFY)}

#define STRINGIFY(X)   #X,
#define CONCATOK(X, Y) X##Y

// Returns the aproximated base 10 length of an integer. This macro returns a constant upper bound
// of the length, as to permit static buffer allocation without worry of overflow.
#define STRLEN_FOR_INT_TYPE(t) \
    (((t)-1 < 0) ? STRLEN_FOR_SIGNED_TYPE(t) : STRLEN_FOR_UNSIGNED_TYPE(t))

#define STRLEN_FOR_UNSIGNED_TYPE(t) (((((sizeof(t) * CHAR_BIT)) * 1233) >> 12) + 1)
#define STRLEN_FOR_SIGNED_TYPE(t)   (STRLEN_FOR_UNSIGNED_TYPE(t) + 1)

// Activate assertsions and unreachable macros in debug builds
#ifndef NDEBUG

    #include <stdio.h>

    #define ASSERT(cond, msg)                                                              \
        do {                                                                               \
            if(!(cond)) {                                                                  \
                fprintf(stderr, "%s[%d]@%s(): assertion failed: %s\n", __FILE__, __LINE__, \
                        __func__, msg);                                                    \
                abort();                                                                   \
            }                                                                              \
        } while(0)

    #define UNREACHABLE()                                                                   \
        do {                                                                                \
            fprintf(stderr, "%s[%d]@%s(): reached unreachable code.\n", __FILE__, __LINE__, \
                    __func__);                                                              \
            abort();                                                                        \
        } while(0)

#else

    #define ASSERT(cond, msg)
    #define UNREACHABLE()

#endif

#endif