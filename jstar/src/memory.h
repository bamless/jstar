#ifndef MEMORY_H
#define MEMORY_H

#include <stdbool.h>
#include <stdlib.h>

#include "object.h"
#include "value.h"
#include "vm.h"

typedef struct JStarVM JStarVM;

#define GC_ALLOC(vm, size) \
    GCallocate(vm, NULL, 0, size)

#define GC_FREE(vm, type, obj) \
    GCallocate(vm, obj, sizeof(type), 0)

#define GC_FREEARRAY(vm, type, obj, count) \
    GCallocate(vm, obj, sizeof(type) * (count), 0)

#define GC_FREE_VAR(vm, type, vartype, count, obj) \
    GCallocate(vm, obj, sizeof(type) + sizeof(vartype) * (count), 0)

void *GCallocate(JStarVM *vm, void *ptr, size_t oldsize, size_t size);

// Launch a garbage collection. It scans all roots (VM stack, global Strings, etc...)
// marking all the reachable objects (recursively, if needed) and then calls freeObjects
// to free all unreached ones.
void garbageCollect(JStarVM *vm);

// Mark an Object/Value as reached
void reachObject(JStarVM *vm, Obj *o);
void reachValue(JStarVM *vm, Value v);

// Free all unmarked objects
void freeObjects(JStarVM *vm);
// Disable the GC
void disableGC(JStarVM *vm, bool disable);

#endif
