#ifndef VECTOR_H
#define VECTOR_H

#include <stdbool.h>
#include <stddef.h>

#include "../jstarconf.h"

#define vecForeach(elem, vec)                                                            \
    for(size_t __cont = 1, __i = 0; __cont && __i < (vec).size; __cont = !__cont, __i++) \
        for(elem = vecIterator(&(vec), __i); __cont; __cont = !__cont)

typedef struct Vector {
    size_t size, capacity;
    void** data;
} Vector;

JSTAR_API Vector vecNew();

JSTAR_API Vector vecCopy(const Vector* vec);
JSTAR_API void vecCopyAssign(Vector* dest, const Vector* src);

JSTAR_API Vector vecMove(Vector* vec);
JSTAR_API void vecMoveAssign(Vector* dest, Vector* src);

JSTAR_API void vecFree(Vector* vec);

JSTAR_API void* vecGet(Vector* vec, size_t i);
JSTAR_API void* vecData(Vector* vec);

JSTAR_API void vecClear(Vector* vec);
JSTAR_API size_t vecPush(Vector* vec, void* elem);
JSTAR_API void vecReserve(Vector* vec, size_t required);
JSTAR_API void vecSet(Vector* vec, size_t i, void* elem);
JSTAR_API void* vecInsert(Vector* vec, size_t i, void* elem);
JSTAR_API void* vecErase(Vector* vec, size_t i);
JSTAR_API void vecPop(Vector* vec);

JSTAR_API bool vecEmpty(Vector* vec);
JSTAR_API size_t vecSize(const Vector* vec);
JSTAR_API size_t vecCapacity(const Vector* vec);

JSTAR_API void* vecBegin(Vector* vec);
JSTAR_API void* vecEnd(Vector* vec);
JSTAR_API void* vecIterator(Vector* vec, size_t i);
JSTAR_API size_t vecIteratorIndex(const Vector* vec, void* it);

#endif
