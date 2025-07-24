#include <stddef.h>
#include <stdlib.h>
#include <string.h>

void* defaultRealloc(void* ptr, size_t oldSz, size_t newSz, void* userData) {
    (void)newSz;
    (void)userData;
    if(newSz == 0) {
        free(ptr);
        return NULL;
    }
    return realloc(ptr, newSz);
}
