#ifndef NATIVE_H
#define NATIVE_H

#include <stdint.h>

#include "object.h"
#include "memory.h"

#define IS_INT(v)      (IS_NUM(v) && (int64_t)AS_NUM(v) == AS_NUM(v))
#define NATIVE(name)   bool name(BlangVM *vm, Value *args, Value *ret)
#define BL_THIS        ((ObjInstance*) AS_OBJ(args[0]))
#define BL_RETURN(val) do { *ret = val; return true; } while(0)

// API functions
void blSetField(BlangVM *vm, ObjInstance *o, const char *name, Value val);
bool blGetField(BlangVM *vm, ObjInstance *o, const char *name, Value *ret);

void blSetGlobal(BlangVM *vm, const char *fname, Value val);
bool blGetGlobal(BlangVM *vm, const char *fname, Value *ret);

#define BL_RAISE_EXCEPTION(vm, cls, err, ...) do { \
		if(!blRaise(vm, cls, err, ##__VA_ARGS__)) { \
			return false; \
		} \
		return true; \
	} while(0)

bool blRaise(BlangVM *vm, const char* cls, const char *errfmt, ...);

#endif
