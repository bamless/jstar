#ifndef OBJECT_H
#define OBJECT_H

#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>

#include "hashtable.h"
#include "chunk.h"
#include "value.h"

#include "util/enum.h"

typedef struct BlangVM BlangVM;

#ifdef DBG_PRINT_GC
DECLARE_TO_STRING(ObjType);
#endif

#define OBJ_TYPE(o) (AS_OBJ(o)->type)

#define IS_BOUND_METHOD(o) (IS_OBJ(o) && OBJ_TYPE(o) == OBJ_BOUND_METHOD)
#define IS_LIST(o)         (IS_OBJ(o) && OBJ_TYPE(o) == OBJ_LIST)
#define IS_STRING(o)       (IS_OBJ(o) && OBJ_TYPE(o) == OBJ_STRING)
#define IS_FUNC(o)         (IS_OBJ(o) && OBJ_TYPE(o) == OBJ_FUNCTION)
#define IS_NATIVE(o)       (IS_OBJ(o) && OBJ_TYPE(o) == OBJ_NATIVE)
#define IS_CLASS(o)        (IS_OBJ(o) && OBJ_TYPE(o) == OBJ_CLASS)
#define IS_INSTANCE(o)     (IS_OBJ(o) && OBJ_TYPE(o) == OBJ_INST)
#define IS_MODULE(o)       (IS_OBJ(o) && OBJ_TYPE(o) == OBJ_MODULE)

#define AS_BOUND_METHOD(o) ((ObjBoundMethod*) AS_OBJ(o))
#define AS_LIST(o)         ((ObjList*)        AS_OBJ(o))
#define AS_STRING(o)       ((ObjString*)      AS_OBJ(o))
#define AS_FUNC(o)         ((ObjFunction*)    AS_OBJ(o))
#define AS_NATIVE(o)       ((ObjNative*)      AS_OBJ(o))
#define AS_CLASS(o)        ((ObjClass*)       AS_OBJ(o))
#define AS_INSTANCE(o)     ((ObjInstance*)    AS_OBJ(o))
#define AS_MODULE(o)       ((ObjModule*)      AS_OBJ(o))

#define OBJTYPE(X) \
	X(OBJ_STRING) X(OBJ_NATIVE) X(OBJ_FUNCTION) X(OBJ_CLASS) X(OBJ_INST) \
	X(OBJ_MODULE) X(OBJ_LIST) X(OBJ_BOUND_METHOD)

DEFINE_ENUM(ObjType, OBJTYPE);

typedef struct ObjClass ObjClass;

typedef struct Obj {
	ObjType type;
	bool reached;
	struct ObjClass *cls;
	struct Obj *next;
} Obj;

typedef struct ObjString {
	Obj base;
	size_t length;
	char *data;
	uint32_t hash;
} ObjString;

typedef struct ObjModule {
	Obj base;
	ObjString *name;
	HashTable globals;
} ObjModule;

typedef struct ObjFunction {
	Obj base;
	uint8_t argsCount;
	Chunk chunk;
	ObjString *name;
	ObjModule *module;
} ObjFunction;

typedef bool (*Native)(BlangVM *vm, Value *argv, Value *ret);

typedef struct ObjNative {
	Obj base;
	uint8_t argsCount;
	Native fn;
	ObjString *name;
	ObjModule *module;
} ObjNative;

typedef struct ObjClass {
	Obj base;
	ObjString *name;
	struct ObjClass *superCls;
	HashTable methods;
} ObjClass;

typedef struct ObjInstance {
	Obj base;
	HashTable fields;
} ObjInstance;

typedef struct ObjList {
	Obj base;
	size_t size;
	size_t count;
	Value *arr;
} ObjList;

typedef struct ObjBoundMethod {
	Obj base;
	Value bound;
	Obj *method;
} ObjBoundMethod;

void printObj(Obj *o);

#endif
