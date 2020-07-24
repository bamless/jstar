#ifndef VECTOR_H
#define VECTOR_H

#include <stdbool.h>
#include <stddef.h>

#define vecForeach(elem, vec)                                                            \
    for(size_t __cont = 1, __i = 0; __cont && __i < (vec).size; __cont = !__cont, __i++) \
        for(elem = vecIterator(&(vec), __i); __cont; __cont = !__cont)

typedef struct Vector {
    size_t size, capacity;
    void** data;
} Vector;

Vector vecNew();

Vector vecCopy(const Vector* vec);
void vecCopyAssign(Vector* dest, const Vector* src);

Vector vecMove(Vector* vec);
void vecMoveAssign(Vector* dest, Vector* src);

void vecFree(Vector* vec);

void* vecGet(Vector* vec, size_t i);
void* vecData(Vector* vec);

void vecClear(Vector* vec);
size_t vecPush(Vector* vec, void* elem);
void vecReserve(Vector* vec, size_t required);
void vecSet(Vector* vec, size_t i, void* elem);
void* vecInsert(Vector* vec, size_t i, void* elem);
void* vecErase(Vector* vec, size_t i);
void vecPop(Vector* vec);

bool vecEmpty(Vector* vec);
size_t vecSize(const Vector* vec);
size_t vecCapacity(const Vector* vec);

void* vecBegin(Vector* vec);
void* vecEnd(Vector* vec);
void* vecIterator(Vector* vec, size_t i);
size_t vecIteratorIndex(const Vector* vec, void* it);

#endif
