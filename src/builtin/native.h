#ifndef NATIVE_H
#define NATIVE_H

#include <stdint.h>
#include <string.h>

#include "memory.h"

#define NATIVE(name) Value name(VM *vm, uint8_t argc, Value *args)

#define IS_INT(v) (IS_NUM(v) && (int64_t)AS_NUM(v) == AS_NUM(v))

#define BL_THIS ((ObjInstance*) AS_OBJ(args[0]))

void blSetField(VM *vm, ObjInstance *o, const char *name, Value val);
bool blGetField(VM *vm, ObjInstance *o, const char *name, Value *ret);

void blSetGlobal(VM *vm, const char *fname, Value val);
bool blGetGlobal(VM *vm, const char *fname, Value *ret);

void blRuntimeError(VM *vm, const char* format, ...);

#endif
