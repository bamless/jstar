#ifndef ARRAY_H
#define ARRAY_H

#include <stdlib.h>  // IWYU pragma: keep
#include <string.h>  // IWYU pragma: keep

#include "jstar/conf.h"

#define ARRAY_INIT_CAP           8
#define arrayForeach(T, it, arr) for(T *it = (arr)->items; it < (arr)->items + (arr)->count; ++it)

#define arrayReserve(arr, newCapacity)                                                     \
    do {                                                                                   \
        if((newCapacity) > (arr)->capacity) {                                              \
            if((arr)->capacity == 0) {                                                     \
                (arr)->capacity = ARRAY_INIT_CAP;                                          \
            }                                                                              \
            while((newCapacity) > (arr)->capacity) {                                       \
                (arr)->capacity *= 2;                                                      \
            }                                                                              \
            (arr)->items = realloc((arr)->items, (arr)->capacity * sizeof(*(arr)->items)); \
            JSR_ASSERT((arr)->items, "Out of memory");                                     \
        }                                                                                  \
    } while(0)

#define arrayAppend(arr, item)                 \
    do {                                       \
        arrayReserve((arr), (arr)->count + 1); \
        (arr)->items[(arr)->count++] = (item); \
    } while(0)

#define arrayFree(arr)                \
    do {                              \
        free((arr)->items);           \
        memset(arr, 0, sizeof(*arr)); \
    } while(0)

#endif
