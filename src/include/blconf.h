#ifndef BLCONF_H
#define BLCONF_H

// Version
#define BLANG_VERSION_MAJOR 0
#define BLANG_VERSION_MINOR 3
#define BLANG_VERSION_PATCH 9

// Increasing version number, used for range checking
#define BLANG_VERSION \
    (BLANG_VERSION_MAJOR * 100000 + BLANG_VERSION_MINOR * 1000 + BLANG_VERSION_PATCH)

// Options
#define USE_COMPUTED_GOTOS
#define NAN_TAGGING
/* #undef DBG_PRINT_EXEC */
/* #undef DBG_PRINT_GC */
/* #undef DBG_STRESS_GC */

#ifndef BLANG_STATIC
#  if defined(_WIN32) && defined(_MSC_VER)
#    ifdef libblang_EXPORTS
#      define BLANG_API  __declspec(dllexport)
#    else
#      define BLANG_API  __declspec(dllimport)
#    endif
#  else
#    define BLANG_API
#  endif
#else
#  define BLANG_API
#endif

#endif
