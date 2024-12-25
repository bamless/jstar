#ifndef JSTAR_CONF_H
#define JSTAR_CONF_H

// Version
#define JSTAR_VERSION_MAJOR  2
#define JSTAR_VERSION_MINOR  0
#define JSTAR_VERSION_PATCH  0
#define JSTAR_VERSION_STRING "2.0.0"

// Increasing version number, used for range checking
#define JSTAR_VERSION \
    (JSTAR_VERSION_MAJOR * 100000 + JSTAR_VERSION_MINOR * 1000 + JSTAR_VERSION_PATCH)

// compiler and platform on which this J* binary was compiled
#define JSTAR_COMPILER "GNU 14.2.1"
#define JSTAR_PLATFORM "Linux"

// Options
#define JSTAR_COMPUTED_GOTOS
/* #undef JSTAR_NAN_TAGGING */
/* #undef JSTAR_DBG_PRINT_EXEC */
/* #undef JSTAR_DBG_PRINT_GC */
/* #undef JSTAR_DBG_STRESS_GC */
/* #undef JSTAR_DBG_CACHE_STATS */

#define JSTAR_SYS
#define JSTAR_IO
#define JSTAR_MATH
#define JSTAR_DEBUG
#define JSTAR_RE

// Platform detection
#if defined(_WIN32) && (defined(__WIN32__) || defined(WIN32) || defined(__MINGW32__))
    #define JSTAR_WINDOWS
#elif defined(__linux__)
    #define JSTAR_LINUX
    #define JSTAR_POSIX
#elif defined(__ANDROID__)
    #define JSTAR_ANDROID
    #define JSTAR_POSIX
#elif defined(__FreeBSD__)
    #define JSTAR_FREEBSD
    #define JSTAR_POSIX
#elif defined(__OpenBSD__)
    #define JSTAR_OPENBSD
    #define JSTAR_POSIX
#elif defined(__EMSCRIPTEN__)
    #define JSTAR_EMSCRIPTEN
#elif defined(__APPLE__) || defined(__MACH__)
    #include <TargetConditionals.h>

    #if TARGET_OS_IPHONE == 1
        #define JSTAR_IOS
    #elif TARGET_OS_MAC == 1
        #define JSTAR_MACOS
    #endif

    #define JSTAR_POSIX
#endif

// Macro for symbol exporting
#ifndef JSTAR_STATIC
    #if defined(_WIN32) && defined(_MSC_VER)
        #if defined(jstar_EXPORTS)
            #define JSTAR_API __declspec(dllexport)
        #else
            #define JSTAR_API __declspec(dllimport)
        #endif
    #elif defined(__GNUC__)
        #if defined(jstar_EXPORTS)
            #define JSTAR_API __attribute__((visibility("default")))
        #else
            #define JSTAR_API
        #endif
    #else
        #define JSTAR_API
    #endif
#else
    #define JSTAR_API
#endif

// Debug assertions
#ifndef NDEBUG
    #include <stdio.h>
    #include <stdlib.h>

    #define JSR_ASSERT(cond, msg)                                                               \
        ((cond) ? ((void)0)                                                                     \
                : (fprintf(stderr, "%s [line:%d] in %s(): %s failed: %s\n", __FILE__, __LINE__, \
                           __func__, #cond, msg),                                               \
                   abort()))

    #define JSR_UNREACHABLE()                                                                     \
        (fprintf(stderr, "%s [line:%d] in %s(): Reached unreachable code.\n", __FILE__, __LINE__, \
                 __func__),                                                                       \
         abort())

#else
    #define JSR_ASSERT(cond, msg) ((void)0)
    #define JSR_UNREACHABLE()     ((void)0)
#endif

#endif
