#ifndef UTIL_H
#define UTIL_H

#include <limits.h>
#include <stdlib.h>
#include <stdint.h>

// Reinterprets the bits of the value `v` from type F to type T
#define REINTERPRET_CAST(F, T, v) ((union {F from; T to;}){.from = (v)}.to)

// Compute the approximate maximal length of an integral type in base 10
// The computed value is an integer constant in order to permit stack buffer allocation
#define STRLEN_FOR_INT(t)      (((t)-1 < 0) ? STRLEN_FOR_SIGNED(t) : STRLEN_FOR_UNSIGNED(t))
#define STRLEN_FOR_SIGNED(t)   (STRLEN_FOR_UNSIGNED(t) + 1)
#define STRLEN_FOR_UNSIGNED(t) (((((sizeof(t) * CHAR_BIT)) * 1233) >> 12) + 1)

// Returns whether `num` has a valid integer representation
#define HAS_INT_REPR(num) ((num) >= (double)INT64_MIN && (num) < -(double)INT64_MIN) 

// Utility function to hash arbitrary data
static inline uint32_t hashBytes(const void* data, size_t length) {
    const char* str = data;
    uint32_t hash = 2166136261u;
    for(size_t i = 0; i < length; i++) {
        hash ^= str[i];
        hash *= 16777619;
    }
    return hash;
}

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