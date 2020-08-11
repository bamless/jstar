#include "jsrparse/vector.h"

#include <memory.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"

#define VEC_GROW_FACTOR 2
#define VEC_INIT_SIZE   1

static void reset(Vector* vec) {
    vec->size = 0;
    vec->capacity = 0;
    vec->data = NULL;
}

static void reallocVector(Vector* vec, size_t capacity) {
    vec->capacity = capacity;
    vec->data = realloc(vec->data, sizeof(void*) * capacity);
    ASSERT(vec->data, "realloc failed");
}

static void growVector(Vector* vec) {
    size_t newCapacity = vec->capacity ? vec->capacity * VEC_GROW_FACTOR : VEC_INIT_SIZE;
    reallocVector(vec, newCapacity);
}

static bool shouldGrow(Vector* vec, size_t required) {
    return vec->size + required > vec->capacity;
}

Vector vecNew() {
    Vector vec;
    reset(&vec);
    return vec;
}

Vector vecCopy(const Vector* vec) {
    Vector copy;
    reset(&copy);
    vecReserve(&copy, vec->size);
    memcpy(copy.data, vec->data, sizeof(void*) * vec->size);
    copy.size = vec->size;
    return copy;
}

void vecCopyAssign(Vector* dest, const Vector* src) {
    free(dest->data);
    reset(dest);
    vecReserve(dest, src->size);
    memcpy(dest->data, src->data, sizeof(void*) * src->size);
    dest->size = src->size;
}

Vector vecMove(Vector* vec) {
    Vector move = *vec;
    reset(vec);
    return move;
}

void vecMoveAssign(Vector* dest, Vector* src) {
    vecFree(dest);
    *dest = *src;
    reset(src);
}

void vecFree(Vector* vec) {
    free(vec->data);
    reset(vec);
}

void* vecGet(Vector* vec, size_t i) {
    ASSERT(i < vec->size, "index out of bounds");
    return vec->data[i];
}

void* vecData(Vector* vec) {
    return vec->data;
}

void vecClear(Vector* vec) {
    vec->size = 0;
}

size_t vecPush(Vector* vec, void* elem) {
    if(shouldGrow(vec, 1)) growVector(vec);
    vec->data[vec->size] = elem;
    return vec->size++;
}

void vecReserve(Vector* vec, size_t required) {
    if(!shouldGrow(vec, required)) return;
    size_t newCapacity = vec->capacity ? vec->capacity * VEC_GROW_FACTOR : VEC_INIT_SIZE;
    while(newCapacity < vec->size + required) {
        newCapacity <<= 1;
    }
    reallocVector(vec, newCapacity);
}

void vecSet(Vector* vec, size_t i, void* elem) {
    ASSERT(i < vec->size, "index out of bounds");
    vec->data[i] = elem;
}

void* vecInsert(Vector* vec, size_t i, void* elem) {
    ASSERT(i <= vec->size, "index out of bounds");
    if(shouldGrow(vec, 1)) growVector(vec);
    size_t shiftRight = (vec->size - i) * sizeof(void*);
    void** insertIt = vec->data + i;
    memmove(vec->data + i + 1, insertIt, shiftRight);
    *insertIt = elem;
    vec->size++;
    return insertIt;
}

void* vecErase(Vector* vec, size_t i) {
    ASSERT(i < vec->size, "index out of bounds");
    size_t shiftLeft = (vec->size - i - 1) * sizeof(void*);
    void** removeIt = vec->data + i;
    memmove(removeIt, vec->data + i + 1, shiftLeft);
    vec->size--;
    return removeIt;
}

void vecPop(Vector* vec) {
    ASSERT(vec->size, "empty vector");
    vec->size--;
}

bool vecEmpty(Vector* vec) {
    return vec->size == 0;
}

size_t vecSize(const Vector* vec) {
    return vec->size;
}

size_t vecCapacity(const Vector* vec) {
    return vec->capacity;
}

void* vecBegin(Vector* vec) {
    return vec->data;
}

void* vecEnd(Vector* vec) {
    return vec->data + vec->size;
}

void* vecIterator(Vector* vec, size_t i) {
    ASSERT(i <= vec->size, "index out of bounds");
    return vec->data + i;
}

size_t vecIteratorIndex(const Vector* vec, void* it) {
    ptrdiff_t i = (intptr_t)it - (intptr_t)vec->data;
    ASSERT(i >= 0, "invalid iterator");
    return i / sizeof(void*);
}
