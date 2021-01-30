#ifndef COMMON_H
#define COMMON_H

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

// -----------------------------------------------------------------------------
// UTILITY FUNCTIONS
// -----------------------------------------------------------------------------

// Returns the closest power of two to n, be it 2^x, where 2^x >= n
static inline int powerOf2Ceil(int n) {
    n--;
    n |= n >> 1;
    n |= n >> 2;
    n |= n >> 4;
    n |= n >> 8;
    n |= n >> 16;
    n++;
    return n;
}

// Hash a string
static inline uint32_t hashString(const char* str, size_t length) {
    uint32_t hash = 2166136261u;
    for(size_t i = 0; i < length; i++) {
        hash ^= str[i];
        hash *= 16777619;
    }
    if(hash < 2) hash += 2;  // Reserve hash value 1 and 0
    return hash;
}

static inline size_t roundUp(size_t num, size_t multiple) {
    return ((num + multiple - 1) / multiple) * multiple;
}

// -----------------------------------------------------------------------------
// DEBUG ASSERTIONS
// -----------------------------------------------------------------------------

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