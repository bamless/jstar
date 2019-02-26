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

// Blang memory allocator. It is a wrapper around realloc that keeps track of
// The number of bytes allocated and starts a garbage collection when needed
void *GCallocate(BlangVM *vm, void *ptr, size_t oldsize, size_t size);
void garbageCollect(BlangVM *vm);

// Function for allocating objects.
// These functions use GCallocate to acquire memory and then init the objects
// with the supplied arguments, as well as initializing all the bookkeping
// nformation needed by the garbage collector (see struct Obj)
ObjNative *newNative(BlangVM *vm, ObjModule *module, ObjString *name, uint8_t argc, Native fn, uint8_t defaultc);
ObjFunction *newFunction(BlangVM *vm, ObjModule *module, ObjString *name, uint8_t argc, uint8_t defaultc);
ObjBoundMethod *newBoundMethod(BlangVM *vm, Value b, Obj *method);
ObjClass *newClass(BlangVM *vm, ObjString *name, ObjClass *superCls);
ObjString *newString(BlangVM *vm, char *cstring, size_t size);
ObjInstance *newInstance(BlangVM *vm, ObjClass *cls);
ObjModule *newModule(BlangVM *vm, ObjString *name);
ObjList *newList(BlangVM *vm, size_t startSize);

// Utility function for creating an ObjString from a regular cstring.
ObjString *copyString(BlangVM *vm, const char *str, size_t length);
// Function for creating an ObjString from a char* allocated using GCallocate.
// Once this funcion is called the buffer passed as input should be considered
// invalid (it may be freed if an identical string is already present in the
// runtime, see ObjString documentation for more details)
ObjString *newStringFromBuf(BlangVM *vm, char *buf, size_t length);

void listAppend(BlangVM *vm, ObjList *lst, Value v);
void listInsert(BlangVM *vm, ObjList *lst, size_t index, Value val);
void listRemove(BlangVM *vm, ObjList *lst, size_t index);

void disableGC(BlangVM *vm, bool disable);
void freeObjects(BlangVM *vm);

void reachObject(BlangVM *vm, Obj *o);
void reachValue(BlangVM *vm, Value v);

#endif
