#ifndef OBJECT_H
#define OBJECT_H

#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>

#include "chunk.h"
#include "value.h"

extern const char *typeName[];

#define OBJ_TYPE(o)  (AS_OBJ(o)->type)

#define IS_STRING(o) (IS_OBJ(o) && OBJ_TYPE(o) == OBJ_STRING)
#define IS_FUNC(o)   (IS_OBJ(o) && OBJ_TYPE(o) == OBJ_FUNCTION)
#define IS_NATIVE(o) (IS_OBJ(o) && OBJ_TYPE(o) == OBJ_NATIVE)

#define AS_STRING(o) ((ObjString*)   AS_OBJ(o))
#define AS_FUNC(o)   ((ObjFunction*) AS_OBJ(o))
#define AS_NATIVE(o) ((ObjNative*)   AS_OBJ(o))

typedef enum ObjType {
	OBJ_STRING, OBJ_NATIVE, OBJ_FUNCTION
} ObjType;

typedef struct Obj {
	ObjType type;
	bool reached;
	struct Obj *next;
} Obj;

typedef struct ObjString {
	Obj base;
	size_t length;
	char *data;
	uint32_t hash;
} ObjString;

typedef struct ObjFunction {
	Obj base;
	uint8_t argsCount;
	Chunk chunk;
	ObjString *name;
} ObjFunction;

typedef Value (*Native)(int argc, Value *argv);

typedef struct ObjNative {
	Obj base;
	uint8_t argsCount;
	Native fn;
	ObjString *name;
} ObjNative;

void printObj(Obj *o);

#endif
