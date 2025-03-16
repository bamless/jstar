#ifndef UTIL_H
#define UTIL_H

#include <limits.h>
#include <stdint.h>
#include <stdlib.h>

#define ARRAY_DEF_CAP 8

// Append an item to a dynamic array, resizing it if necessary, using gcAlloc
#define ARRAY_GC_APPEND(vm, arr, count, cap, items, val)                             \
    do {                                                                             \
        if((arr)->count >= (arr)->cap) {                                             \
            size_t oldCap = (arr)->cap;                                              \
            (arr)->cap = (arr)->cap == 0 ? ARRAY_DEF_CAP : (arr)->cap * 2;           \
            (arr)->items = gcAlloc(vm, (arr)->items, oldCap * sizeof(*(arr)->items), \
                                   (arr)->cap * sizeof(*(arr)->items));              \
            JSR_ASSERT((arr)->items, "Out of memory");                               \
        }                                                                            \
        (arr)->items[(arr)->count++] = (val);                                        \
    } while(0)

// Append an item to a dynamic array, resizing it if necessary, using realloc
#define ARRAY_APPEND(arr, count, cap, items, val)                                     \
    do {                                                                              \
        if((arr)->count >= (arr)->cap) {                                              \
            (arr)->cap = (arr)->cap == 0 ? ARRAY_DEF_CAP : (arr)->cap * 2;            \
            (arr)->items = realloc((arr)->items, (arr)->cap * sizeof(*(arr)->items)); \
            JSR_ASSERT((arr)->items, "Out of memory");                                \
        }                                                                             \
        (arr)->items[(arr)->count++] = (val);                                         \
    } while(0)

// Reinterprets the bits of the value `v` from type F to type T
#define REINTERPRET_CAST(F, T, v) \
    ((union {                     \
         F from;                  \
         T to;                    \
     }){.from = (v)}              \
         .to)

// Compute the approximate maximal length of an integral type in base 10
// The computed value is an integer constant in order to permit stack buffer allocation
#define STRLEN_FOR_INT(t)      (((t) - 1 < 0) ? STRLEN_FOR_SIGNED(t) : STRLEN_FOR_UNSIGNED(t))
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

#endif
