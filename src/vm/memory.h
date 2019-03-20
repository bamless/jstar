#ifndef MEMORY_H
#define MEMORY_H

#include "object.h"

#include <stdlib.h>
#include <stdbool.h>

typedef struct Frame Frame;
typedef struct BlangVM BlangVM;

void garbageCollect(BlangVM *vm);

// Function for allocating objects.
// These functions use GCallocate to acquire memory and then init the objects
// with the supplied arguments, as well as initializing all the bookkeping
// nformation needed by the garbage collector (see struct Obj)
ObjNative *newNative(BlangVM *vm, ObjModule *module, ObjString *name, uint8_t argc, Native fn, uint8_t defaultc);
ObjFunction *newFunction(BlangVM *vm, ObjModule *module, ObjString *name, uint8_t argc, uint8_t defaultc);
ObjBoundMethod *newBoundMethod(BlangVM *vm, Value b, Obj *method);
ObjClass *newClass(BlangVM *vm, ObjString *name, ObjClass *superCls);
ObjInstance *newInstance(BlangVM *vm, ObjClass *cls);
ObjClosure *newClosure(BlangVM *vm, ObjFunction *fn);
ObjModule *newModule(BlangVM *vm, ObjString *name);
ObjUpvalue *newUpvalue(BlangVM *vm, Value *addr);
ObjList *newList(BlangVM *vm, size_t startSize);
ObjStackTrace *newStackTrace(BlangVM *vm);

ObjString *allocateString(BlangVM *vm, size_t length);
void reallocateString(BlangVM *vm, ObjString *str, size_t newLen);
ObjString *copyString(BlangVM *vm, const char *str, size_t length, bool intern);

uint32_t stringGetHash(ObjString *str);

void stRecordFrame(BlangVM *vm, ObjStackTrace *st, Frame *f, int depth);

void listAppend(BlangVM *vm, ObjList *lst, Value v);
void listInsert(BlangVM *vm, ObjList *lst, size_t index, Value val);
void listRemove(BlangVM *vm, ObjList *lst, size_t index);

void disableGC(BlangVM *vm, bool disable);
void freeObjects(BlangVM *vm);

void reachObject(BlangVM *vm, Obj *o);
void reachValue(BlangVM *vm, Value v);

#endif
