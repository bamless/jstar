#ifndef MEMORY_H
#define MEMORY_H

#include "object.h"

#include <stdlib.h>
#include <stdbool.h>

typedef struct VM VM; //forward declaration needed for circular dependecy

typedef struct MemManager {
	VM *vm;
	size_t allocated;
	Obj *objects;
	bool disableGC;
} MemManager;

void initMemoryManager(MemManager *m, VM *vm);
void freeMemoryManager(MemManager *m);

void *allocate(MemManager *m, void *ptr, size_t oldsize, size_t length);
ObjString *newString(MemManager *m, char *cstring, size_t size);
ObjFunction *newFunction(MemManager *m, int argsCount);
ObjNative *newNative(MemManager *m, int argsCount, Native fn);

void disableGC(MemManager *m, bool disable);

#endif
