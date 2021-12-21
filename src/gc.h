#ifndef GC_H
#define GC_H

#include <stdlib.h>

#include "jstar.h"
#include "value.h"

// Macros to simplify memory allocation
#define GC_ALLOC(vm, size)                  gcAlloc(vm, NULL, 0, size)
#define GC_FREE(vm, t, obj)                 gcAlloc(vm, obj, sizeof(t), 0)
#define GC_FREE_ARRAY(vm, t, obj, count)    gcAlloc(vm, obj, sizeof(t) * (count), 0)
#define GC_FREE_VAR(vm, t, var, count, obj) gcAlloc(vm, obj, sizeof(t) + sizeof(var) * (count), 0)

// Allocate (or reallocate) some memory using the J* garbage collector.
// This memory is owned by the GC, but can't be collected until is is exposed as a Value in the
// runtime (for example as an Obj*, or as a part of one).
void* gcAlloc(JStarVM* vm, void* ptr, size_t oldsize, size_t size);

// Launch a garbage collection. It scans all roots (VM stack, global Strings, etc...)
// marking all the reachable objects (recursively, if needed) and then calls sweepObjects
// to free all unreached ones.
void garbageCollect(JStarVM* vm);

// Mark an Object/Value as reached
void reachObject(JStarVM* vm, Obj* o);
void reachValue(JStarVM* vm, Value v);

// Free all unmarked objects
void sweepObjects(JStarVM* vm);

#endif
