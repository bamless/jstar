#include "blang.h"
#include "vm.h"

#include <stdarg.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

bool checkNum(BlangVM *vm, Value val, const char *name) {
	if(IS_NUM(val)) return true;
	blRaise(vm, "TypeException", "%s must be a number.", name);
	return false;
}

bool checkInt(BlangVM *vm, Value val, const char *name) {
	if(!checkNum(vm, val, name)) return false;
	double num = AS_NUM(val);
	if(trunc(num) == num) return true;
	blRaise(vm, "TypeException", "%s must be an integer.", name);
	return false;
}

bool checkStr(BlangVM *vm, Value val, const char *name) {
	if(IS_STRING(val)) return true;
	blRaise(vm, "TypeException", "%s must be a String.", name);
	return false;
}

bool checkList(BlangVM *vm, Value val, const char *name) {
	if(IS_LIST(val)) return true;
	blRaise(vm, "TypeException", "%s must be a List.", name);
	return false;
}


size_t checkIndex(BlangVM *vm, Value val, size_t max, const char *name) {
	if(!checkInt(vm, val, name)) return SIZE_MAX;
	double idx = AS_NUM(val);
	if(idx >= 0 && idx < max) return (size_t) idx;
	blRaise(vm, "IndexOutOfBoundException", "index out of bounds: %g.", idx);
	return SIZE_MAX;
}

void blSetField(BlangVM *vm, ObjInstance *o, const char *name, Value val) {
	push(vm, val);
	push(vm, OBJ_VAL(o));
	hashTablePut(&o->fields, copyString(vm, name, strlen(name), true), val);
	pop(vm);
	pop(vm);
}

bool blGetField(BlangVM *vm, ObjInstance *o, const char *name, Value *ret) {
	push(vm, OBJ_VAL(o));
	bool found = hashTableGet(&o->fields, copyString(vm, name, strlen(name), true), ret);
	pop(vm);
	return found;
}

void blSetGlobal(BlangVM *vm, const char *fname, Value val) {
	push(vm, val);
	hashTablePut(&vm->module->globals, copyString(vm, fname, strlen(fname), true), val);
	pop(vm);
}

bool blGetGlobal(BlangVM *vm, const char *fname, Value *ret) {
	ObjString *name = copyString(vm, fname, strlen(fname), true);
	if(!hashTableGet(&vm->module->globals, name, ret)) {
		return hashTableGet(&vm->core->globals, name, ret);
	}
	return true;
}

bool blRaise(BlangVM *vm, const char* cls, const char *err, ...) {
	Value excVal;
	if(!(blGetGlobal(vm, cls, &excVal) && IS_CLASS(excVal))) {
		return false;
	}

	ObjInstance *excInst = newInstance(vm, AS_CLASS(excVal));
	push(vm, OBJ_VAL(excInst));

	ObjStackTrace *st = newStackTrace(vm);
	hashTablePut(&excInst->fields, vm->stField, OBJ_VAL(st));

	if(err != NULL) {
		char errStr[1024] = {0};
		va_list args;
		va_start(args, err);
		vsnprintf(errStr, sizeof(errStr) - 1, err, args);
		va_end(args);

		blSetField(vm, excInst, "err", OBJ_VAL(copyString(vm, errStr, strlen(errStr), false)));
	}

	pop(vm);
	vm->exception = excInst;

	return true;
}
