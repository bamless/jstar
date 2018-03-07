#ifndef MEMORY_H
#define MEMORY_H

#include "object.h"

#include <stdlib.h>
#include <stdbool.h>

typedef struct VM VM;

#define ALLOC(vm, size) allocate(vm, NULL, 0, size)

#define FREE(vm, type, obj) allocate(vm, obj, sizeof(type), 0)
#define FREEARRAY(vm, type, obj, count) \
		allocate(vm, obj, sizeof(type) * count, 0)

void *allocate(VM *vm, void *ptr, size_t oldsize, size_t size);
ObjString *newString(VM *vm, char *cstring, size_t size);
ObjFunction *newFunction(VM *vm, int argsCount);
ObjNative *newNative(VM *vm, int argsCount, Native fn);

ObjString *copyString(VM *vm, const char *str, size_t length);

void disableGC(VM *vm, bool disable);
void freeObjects(VM *vm);

void reachObject(VM *vm, Obj *o);
void reachValue(VM *vm, Value v);

#endif
