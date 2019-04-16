#ifndef MEMORY_H
#define MEMORY_H

#include "object.h"

#include <stdlib.h>
#include <stdbool.h>

/**
 * Functions for allocating/freeing garbage collectable objects.
 */

typedef struct Frame Frame;
typedef struct BlangVM BlangVM;
typedef struct BlBuffer BlBuffer;

// Launch a garbage collection. It scans all roots (VM stack, global Strings, etc...)
// marking all the reachable objects (recursively, if needed) and then calls freeObjects
// to free all unreached ones.
void garbageCollect(BlangVM *vm);

/** 
 * Functions for allocating objects.
 * These functions use GCallocate to acquire memory and then initialize
 * the object with the supplied arguments, as well as setting all the 
 * bookkeping information needed by the garbage collector (see struct Obj)
 */

ObjNative *newNative(BlangVM *vm, ObjModule *module, ObjString *name, uint8_t argc, Native fn, uint8_t defaultc);
ObjFunction *newFunction(BlangVM *vm, ObjModule *module, ObjString *name, uint8_t argc, uint8_t defaultc);
ObjRange *newRange(BlangVM *vm, double start, double stop, double step);
ObjClass *newClass(BlangVM *vm, ObjString *name, ObjClass *superCls);
ObjBoundMethod *newBoundMethod(BlangVM *vm, Value b, Obj *method);
ObjInstance *newInstance(BlangVM *vm, ObjClass *cls);
ObjClosure *newClosure(BlangVM *vm, ObjFunction *fn);
ObjModule *newModule(BlangVM *vm, ObjString *name);
ObjUpvalue *newUpvalue(BlangVM *vm, Value *addr);
ObjList *newList(BlangVM *vm, size_t startSize);
ObjTuple *newTuple(BlangVM *vm, size_t size);
ObjStackTrace *newStackTrace(BlangVM *vm);

ObjString *allocateString(BlangVM *vm, size_t length);
ObjString *copyString(BlangVM *vm, const char *str, size_t length, bool intern);

// Dumps a frame in a ObjStackTrace
void stRecordFrame(BlangVM *vm, ObjStackTrace *st, Frame *f, int depth);

// List manipulation functions
void listAppend(BlangVM *vm, ObjList *lst, Value v);
void listInsert(BlangVM *vm, ObjList *lst, size_t index, Value val);
void listRemove(BlangVM *vm, ObjList *lst, size_t index);

ObjString *blBufferToString(BlBuffer *b);

static inline uint32_t hashString(const char *str, size_t length) {
	uint32_t hash = 2166136261u;

	for (size_t i = 0; i < length; i++) {
		hash ^= str[i];
		hash *= 16777619;
	}

	return hash;
}

static inline uint32_t stringGetHash(ObjString *str) {
	if(str->hash == 0) {
		str->hash = hashString(str->data, str->length);
	}
	return str->hash;
}

// Mark an Object/Value as reached
void reachObject(BlangVM *vm, Obj *o);
void reachValue(BlangVM *vm, Value v);

// Free all unmarked objects
void freeObjects(BlangVM *vm);
// Disable the GC
void disableGC(BlangVM *vm, bool disable);

#endif
