#ifndef UTIL_H
#define UTIL_H

#include <limits.h>
#include <stddef.h>
#include <stdint.h>

#include "jstarconf.h"

// Reinterprets the bits of the value `v` from type F to type T
#define REINTERPRET_CAST(F, T, v) ((union {F f; T t;}){.f = (v)}.t)

// Compute the approximate maximal length of an integral type in base 10
// The computed value is an integer constant in order to permit stack buffer allocation
#define STRLEN_FOR_INT(t)      (((t)-1 < 0) ? STRLEN_FOR_SIGNED(t) : STRLEN_FOR_UNSIGNED(t))
#define STRLEN_FOR_SIGNED(t)   (STRLEN_FOR_UNSIGNED(t) + 1)
#define STRLEN_FOR_UNSIGNED(t) (((((sizeof(t) * CHAR_BIT)) * 1233) >> 12) + 1)

// Debug assertions
#ifndef NDEBUG
    #include <stdio.h>
    #include <stdlib.h>

    #define ASSERT(cond, msg)                                                                    \
        ((cond) ?                                                                                \
         ((void)0) :                                                                             \
         (fprintf(stderr, "%s [line:%d] in %s(): %s failed: %s\n", __FILE__, __LINE__, __func__, \
                  #cond, msg), abort()))

    #define UNREACHABLE() \
        (fprintf(stderr, "%s [line:%d] in %s(): Reached unreachable code.\n", __FILE__, __LINE__, \
                 __func__), abort())

#else
    #define ASSERT(cond, msg) ((void)0)
    #define UNREACHABLE()     ((void)0)
#endif

#endif