#ifndef NATIVE_H
#define NATIVE_H

#include <stdint.h>

#include "object.h"
#include "memory.h"

// Macros for native implementation
#define NATIVE(name)     bool name(BlangVM *vm, Value *args, Value *ret)

#define BL_THIS          ((ObjInstance*) AS_OBJ(args[0]))

#define BL_RETURN(val)   do { *ret = val; return true; } while(0)
#define BL_RETURN_TRUE   BL_RETURN(TRUE_VAL)
#define BL_RETURN_FALSE  BL_RETURN(FALSE_VAL)
#define BL_RETURN_OBJ(o) BL_RETURN(OBJ_VAL(o))
#define BL_RETURN_NUM(n) BL_RETURN(NUM_VAL(n))
#define BL_RETURN_NULL   BL_RETURN(NULL_VAL)

#define BL_RAISE_EXCEPTION(vm, cls, err, ...) do { \
		if(!blRaise(vm, cls, err, ##__VA_ARGS__)) { \
			return false; \
		} \
		return true; \
	} while(0)

// API functions
bool checkNum(BlangVM *vm, Value val, const char *name);
bool checkInt(BlangVM *vm, Value val, const char *name);
bool checkStr(BlangVM *vm, Value val, const char *name);
bool checkList(BlangVM *vm, Value val, const char *name);
size_t checkIndex(BlangVM *vm, Value val, size_t max, const char *name);

void blSetField(BlangVM *vm, ObjInstance *o, const char *name, Value val);
bool blGetField(BlangVM *vm, ObjInstance *o, const char *name, Value *ret);

void blSetGlobal(BlangVM *vm, const char *fname, Value val);
bool blGetGlobal(BlangVM *vm, const char *fname, Value *ret);

bool blRaise(BlangVM *vm, const char* cls, const char *errfmt, ...);

#endif
