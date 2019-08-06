#ifndef MEMORY_H
#define MEMORY_H

#include "object.h"
#include "util.h"

#include <stdbool.h>
#include <stdlib.h>

typedef struct Frame Frame;
typedef struct JStarVM JStarVM;
typedef struct JStarBuffer JStarBuffer;

// Launch a garbage collection. It scans all roots (VM stack, global Strings, etc...)
// marking all the reachable objects (recursively, if needed) and then calls freeObjects
// to free all unreached ones.
void garbageCollect(JStarVM *vm);

// Functions for allocating objects.
// These functions use GCallocate to acquire memory and then initialize
// the object with the supplied arguments, as well as setting all the
// bookkeping information needed by the garbage collector (see struct Obj)
ObjNative       *newNative(JStarVM *vm, ObjModule *module, ObjString *name, uint8_t argc, JStarNative fn, 
                           uint8_t defaultc);
ObjFunction     *newFunction(JStarVM *vm, ObjModule *module, ObjString *name, uint8_t argc, 
                             uint8_t defaultc);
ObjClass        *newClass(JStarVM *vm, ObjString *name, ObjClass *superCls);
ObjBoundMethod  *newBoundMethod(JStarVM *vm, Value b, Obj *method);
ObjInstance     *newInstance(JStarVM *vm, ObjClass *cls);
ObjClosure      *newClosure(JStarVM *vm, ObjFunction *fn);
ObjModule       *newModule(JStarVM *vm, ObjString *name);
ObjUpvalue      *newUpvalue(JStarVM *vm, Value *addr);
ObjList         *newList(JStarVM *vm, size_t startSize);
ObjTuple        *newTuple(JStarVM *vm, size_t size);
ObjStackTrace   *newStackTrace(JStarVM *vm);

ObjString       *allocateString(JStarVM *vm, size_t length);
ObjString       *copyString(JStarVM *vm, const char *str, size_t length, bool intern);

// Functions for manipulating objects.
// All these functions require allocating garbage collectable
// memory so are defined here

// Dumps a frame in a ObjStackTrace
void stRecordFrame(JStarVM *vm, ObjStackTrace *st, Frame *f, int depth);

// ObjList manipulation functions
void listAppend(JStarVM *vm, ObjList *lst, Value v);
void listInsert(JStarVM *vm, ObjList *lst, size_t index, Value val);
void listRemove(JStarVM *vm, ObjList *lst, size_t index);

// Convert a BlBuffer to an ObjString
ObjString *blBufferToString(JStarBuffer *b);

// Mark an Object/Value as reached
void reachObject(JStarVM *vm, Obj *o);
void reachValue(JStarVM *vm, Value v);

// Free all unmarked objects
void freeObjects(JStarVM *vm);
// Disable the GC
void disableGC(JStarVM *vm, bool disable);

#endif
