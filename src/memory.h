#ifndef MEMORY_H
#define MEMORY_H

#include "object.h"

#include <stdlib.h>
#include <stdbool.h>

#define ALLOC(m, size) allocate(m, NULL, 0, size)

#define FREE(m, type, obj) allocate(m, obj, sizeof(type), 0)
#define FREEARRAY(m, type, obj, count) \
		allocate(m, obj, sizeof(type) * count, 0)

typedef struct VM VM; //forward declaration needed for circular dependecy

typedef struct MemManager {
	VM *vm;
	size_t allocated, nextGC;
	Obj *objects;
	bool disableGC;
	Obj **reachedStack;
	size_t reachedCapacity, reachedCount;
} MemManager;

void initMemoryManager(MemManager *m, VM *vm);
void freeMemoryManager(MemManager *m);

void *allocate(MemManager *m, void *ptr, size_t oldsize, size_t size);
ObjString *newString(MemManager *m, char *cstring, size_t size);
ObjFunction *newFunction(MemManager *m, int argsCount);
ObjNative *newNative(MemManager *m, int argsCount, Native fn);

void disableGC(MemManager *m, bool disable);
void freeObjects(MemManager *m);

void reachObject(MemManager *m, Obj *o);
void reachValue(MemManager *m, Value v);


#endif
