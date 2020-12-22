#ifndef COMMON_H
#define COMMON_H

#include <limits.h>
#include <stddef.h>
#include <stdint.h>

#include "jstarconf.h"

// -----------------------------------------------------------------------------
// MACROS TO COMPUTE BASE 10 LENGHT OF INTEGERS
// -----------------------------------------------------------------------------

#define STRLEN_FOR_INT(t)      (((t)-1 < 0) ? STRLEN_FOR_SIGNED(t) : STRLEN_FOR_UNSIGNED(t))
#define STRLEN_FOR_SIGNED(t)   (STRLEN_FOR_UNSIGNED(t) + 1)
#define STRLEN_FOR_UNSIGNED(t) (((((sizeof(t) * CHAR_BIT)) * 1233) >> 12) + 1)

// -----------------------------------------------------------------------------
// DEBUG ASSERTIONS AND UNREACHEABLE
// -----------------------------------------------------------------------------

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
    return hash;
}

static inline size_t roundUp(size_t num, size_t multiple) {
    return ((num + multiple - 1) / multiple) * multiple;
}

#endif