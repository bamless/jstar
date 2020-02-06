#ifndef JSTARCONF_H
#define JSTARCONF_H

// Version
#define JSTAR_VERSION_MAJOR 0
#define JSTAR_VERSION_MINOR 9
#define JSTAR_VERSION_PATCH 10
#define JSTAR_VERSION_STRING "0.9.10"

// Increasing version number, used for range checking
#define JSTAR_VERSION \
    (JSTAR_VERSION_MAJOR * 100000 + JSTAR_VERSION_MINOR * 1000 + JSTAR_VERSION_PATCH)

// compiler and platform on which this J* binary was compiled
#define JSTAR_COMPILER "GNU 9.2.0"
#define JSTAR_PLATFORM "Linux"

// Options
#define USE_COMPUTED_GOTOS
#define NAN_TAGGING
/* #undef DBG_PRINT_EXEC */
/* #undef DBG_PRINT_GC */
/* #undef DBG_STRESS_GC */

#define JSTAR_IO
#define JSTAR_MATH
#define JSTAR_DEBUG
#define JSTAR_RE

#ifndef JSTAR_STATIC
    #if defined(_WIN32) && defined(_MSC_VER)
        #if defined(libjstar_EXPORTS) || defined(NATIVE_EXPORTS)
            #define JSTAR_API __declspec(dllexport)
        #else
            #define JSTAR_API __declspec(dllimport)
        #endif
    #else
        #define JSTAR_API
    #endif
#else
    #define JSTAR_API
#endif

#endif
