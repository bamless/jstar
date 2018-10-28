#ifndef MEMORY_H
#define MEMORY_H

#include "object.h"

#include <stdlib.h>
#include <stdbool.h>

typedef struct BlangVM BlangVM;

#define GC_ALLOC(vm, size) GCallocate(vm, NULL, 0, size)

#define GC_FREE(vm, type, obj) GCallocate(vm, obj, sizeof(type), 0)
#define GC_FREEARRAY(vm, type, obj, count) \
		GCallocate(vm, obj, sizeof(type) * count, 0)

void *GCallocate(BlangVM *vm, void *ptr, size_t oldsize, size_t size);
void garbageCollect(BlangVM *vm);

ObjNative *newNative(BlangVM *vm, ObjModule *module, ObjString *name, uint8_t argsCount, Native fn);
ObjBoundMethod *newBoundMethod(BlangVM *vm, Value b, Obj *method);
ObjFunction *newFunction(BlangVM *vm, ObjModule *module, ObjString *name, uint8_t argsCount);
ObjClass *newClass(BlangVM *vm, ObjString *name, ObjClass *superCls);
ObjString *newString(BlangVM *vm, char *cstring, size_t size);
ObjInstance *newInstance(BlangVM *vm, ObjClass *cls);
ObjModule *newModule(BlangVM *vm, ObjString *name);
ObjList *newList(BlangVM *vm, size_t startSize);

ObjString *copyString(BlangVM *vm, const char *str, size_t length);
ObjString *newStringFromBuf(BlangVM *vm, char *buf, size_t length);

void listAppend(BlangVM *vm, ObjList *lst, Value v);
void listInsert(BlangVM *vm, ObjList *lst, size_t index, Value val);
void listRemove(BlangVM *vm, ObjList *lst, size_t index);

void disableGC(BlangVM *vm, bool disable);
void freeObjects(BlangVM *vm);

void reachObject(BlangVM *vm, Obj *o);
void reachValue(BlangVM *vm, Value v);

#endif
