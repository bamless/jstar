#ifndef OBJECT_H
#define OBJECT_H

#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>

#include "chunk.h"

typedef enum ObjType {
	OBJ_STRING, OBJ_NATIVE, OBJ_FUNCTION
} ObjType;

typedef struct Obj {
	ObjType type;
	bool dark;
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
	int argsCount;
	Chunk *code;
	ObjString *name;
} ObjFunction;

#endif
