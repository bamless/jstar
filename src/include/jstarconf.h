#ifndef JSTARCONF_H
#define JSTARCONF_H

// Version
#define JSTAR_VERSION_MAJOR 0
#define JSTAR_VERSION_MINOR 4
#define JSTAR_VERSION_PATCH 1

// Increasing version number, used for range checking
#define JSTAR_VERSION \
    (JSTAR_VERSION_MAJOR * 100000 + JSTAR_VERSION_MINOR * 1000 + JSTAR_VERSION_PATCH)

// Options
#define USE_COMPUTED_GOTOS
#define NAN_TAGGING
/* #undef DBG_PRINT_EXEC */
/* #undef DBG_PRINT_GC */
/* #undef DBG_STRESS_GC */

#ifndef JSTAR_STATIC
#  if defined(_WIN32) && defined(_MSC_VER)
#    if defined(libjstar_EXPORTS) || defined(NATIVE_EXPORTS)
#      define JSTAR_API  __declspec(dllexport)
#    else
#      define JSTAR_API  __declspec(dllimport)
#    endif
#  else
#    define JSTAR_API
#  endif
#else
#  define JSTAR_API
#endif

#endif
