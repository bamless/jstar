#ifndef MEMORY_H
#define MEMORY_H

#include "object.h"
#include "util.h"

#include <stdbool.h>
#include <stdlib.h>

typedef struct Frame Frame;
typedef struct BlangVM BlangVM;
typedef struct BlBuffer BlBuffer;

// Launch a garbage collection. It scans all roots (VM stack, global Strings, etc...)
// marking all the reachable objects (recursively, if needed) and then calls freeObjects
// to free all unreached ones.
void garbageCollect(BlangVM *vm);

// Functions for allocating objects.
// These functions use GCallocate to acquire memory and then initialize
// the object with the supplied arguments, as well as setting all the
// bookkeping information needed by the garbage collector (see struct Obj)
ObjNative *newNative(BlangVM *vm, ObjModule *module, ObjString *name, uint8_t argc, Native fn,
                     uint8_t defaultc);
ObjFunction *newFunction(BlangVM *vm, ObjModule *module, ObjString *name, uint8_t argc,
                         uint8_t defaultc);
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

// Functions for manipulating objects.
// All these functions require allocating garbage collectable
// memory so are defined here

// Dumps a frame in a ObjStackTrace
void stRecordFrame(BlangVM *vm, ObjStackTrace *st, Frame *f, int depth);

// ObjList manipulation functions
void listAppend(BlangVM *vm, ObjList *lst, Value v);
void listInsert(BlangVM *vm, ObjList *lst, size_t index, Value val);
void listRemove(BlangVM *vm, ObjList *lst, size_t index);

// Convert a BlBuffer to an ObjString
ObjString *blBufferToString(BlBuffer *b);

// Mark an Object/Value as reached
void reachObject(BlangVM *vm, Obj *o);
void reachValue(BlangVM *vm, Value v);

// Free all unmarked objects
void freeObjects(BlangVM *vm);
// Disable the GC
void disableGC(BlangVM *vm, bool disable);

#endif
