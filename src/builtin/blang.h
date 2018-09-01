#ifndef NATIVE_H
#define NATIVE_H

#include <stdint.h>

#include "object.h"
#include "memory.h"

#define BLANG_VERSION_STR "1.0.0"

#define BLANG_VERSION_MAJOR 1
#define BLANG_VERSION_MINOR 0
#define BLANG_VERSION_PATCH 0

#define IS_INT(v)      (IS_NUM(v) && (int64_t)AS_NUM(v) == AS_NUM(v))
#define NATIVE(name)   bool name(VM *vm, Value *args, Value *ret)
#define BL_THIS        ((ObjInstance*) AS_OBJ(args[0]))
#define BL_RETURN(val) do { *ret = val; return true; } while(0)

// API functions
void blSetField(VM *vm, ObjInstance *o, const char *name, Value val);
bool blGetField(VM *vm, ObjInstance *o, const char *name, Value *ret);

void blSetGlobal(VM *vm, const char *fname, Value val);
bool blGetGlobal(VM *vm, const char *fname, Value *ret);

#define blRiseException(vm, cls, err, ...) do { \
		if(!blRiseExceptionImpl(vm, cls, err, ##__VA_ARGS__)) { \
			return false; \
		} \
		return true; \
	} while(0)

bool blRiseExceptionImpl(VM *vm, const char* cls, const char *errfmt, ...);

#endif
