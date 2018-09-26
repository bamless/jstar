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
void garbageCollect(VM *vm);

ObjNative *newNative(VM *vm, ObjModule *module, ObjString *name, uint8_t argsCount, Native fn);
ObjBoundMethod *newBoundMethod(VM *vm, Value b, Obj *method);
ObjFunction *newFunction(VM *vm, ObjModule *module, ObjString *name, uint8_t argsCount);
ObjClass *newClass(VM *vm, ObjString *name, ObjClass *superCls);
ObjString *newString(VM *vm, char *cstring, size_t size);
ObjInstance *newInstance(VM *vm, ObjClass *cls);
ObjModule *newModule(VM *vm, ObjString *name);
ObjList *newList(VM *vm, size_t startSize);

ObjString *copyString(VM *vm, const char *str, size_t length);
ObjString *newStringFromBuf(VM *vm, char *buf, size_t length);

void listAppend(VM *vm, ObjList *lst, Value v);
void listInsert(VM *vm, ObjList *lst, size_t index, Value val);
void listRemove(VM *vm, ObjList *lst, size_t index);

void disableGC(VM *vm, bool disable);
void freeObjects(VM *vm);

void reachObject(VM *vm, Obj *o);
void reachValue(VM *vm, Value v);

#endif
