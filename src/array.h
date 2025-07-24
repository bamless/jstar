#ifndef ARRAY_H
#define ARRAY_H

#include <stdlib.h>  // IWYU pragma: keep
#include <string.h>  // IWYU pragma: keep

#include "jstar/conf.h"

#define ARRAY_INIT_CAP           8
#define arrayForeach(T, it, arr) for(T *it = (arr)->items; it < (arr)->items + (arr)->count; ++it)

#define arrayReserve(vm, arr, newCapacity)                                                     \
    do {                                                                                       \
        if((newCapacity) > (arr)->capacity) {                                                  \
            size_t oldCap = (arr)->capacity;                                                   \
            if((arr)->capacity == 0) {                                                         \
                (arr)->capacity = ARRAY_INIT_CAP;                                              \
            }                                                                                  \
            while((newCapacity) > (arr)->capacity) {                                           \
                (arr)->capacity *= 2;                                                          \
            }                                                                                  \
            (arr)->items = vm->realloc((arr)->items, oldCap * sizeof(*(arr)->items),           \
                                       (arr)->capacity * sizeof(*(arr)->items), vm->userData); \
            JSR_ASSERT((arr)->items, "Out of memory");                                         \
        }                                                                                      \
    } while(0)

#define arrayAppend(vm, arr, item)                 \
    do {                                           \
        arrayReserve(vm, (arr), (arr)->count + 1); \
        (arr)->items[(arr)->count++] = (item);     \
    } while(0)

#define arrayFree(vm, arr)                                                                  \
    do {                                                                                    \
        vm->realloc((arr)->items, (arr)->capacity * sizeof((arr)->items), 0, vm->userData); \
        memset(arr, 0, sizeof(*arr));                                                       \
    } while(0)

#define arrayReserveGC(vm, arr, newCapacity)                                         \
    do {                                                                             \
        size_t oldCap = (arr)->capacity;                                             \
        if((newCapacity) > (arr)->capacity) {                                        \
            if((arr)->capacity == 0) {                                               \
                (arr)->capacity = ARRAY_INIT_CAP;                                    \
            }                                                                        \
            while((newCapacity) > (arr)->capacity) {                                 \
                (arr)->capacity *= 2;                                                \
            }                                                                        \
            (arr)->items = gcAlloc(vm, (arr)->items, oldCap * sizeof(*(arr)->items), \
                                   (arr)->capacity * sizeof(*(arr)->items));         \
            JSR_ASSERT((arr)->items, "GC out of memory");                            \
        }                                                                            \
    } while(0)

#define arrayAppendGC(vm, arr, item)                 \
    do {                                             \
        arrayReserveGC(vm, (arr), (arr)->count + 1); \
        (arr)->items[(arr)->count++] = (item);       \
    } while(0)

#define arrayFreeGC(vm, arr)                                                   \
    do {                                                                       \
        gcAlloc(vm, (arr)->items, (arr)->capacity * sizeof(*(arr)->items), 0); \
    } while(0)

#endif
