#include "blang.h"
#include "vm.h"

#include <stdarg.h>
#include <string.h>
#include <stdio.h>

void blSetField(VM *vm, ObjInstance *o, const char *name, Value val) {
	push(vm, val);
	push(vm, OBJ_VAL(o));
	hashTablePut(&o->fields, copyString(vm, name, strlen(name)), val);
	pop(vm);
	pop(vm);
}

bool blGetField(VM *vm, ObjInstance *o, const char *name, Value *ret) {
	push(vm, OBJ_VAL(o));
	bool found = hashTableGet(&o->fields, copyString(vm, name, strlen(name)), ret);
	pop(vm);
	return found;
}

void blSetGlobal(VM *vm, const char *fname, Value val) {
	push(vm, val);
	hashTablePut(&vm->module->globals, copyString(vm, fname, strlen(fname)), val);
	pop(vm);
}

bool blGetGlobal(VM *vm, const char *fname, Value *ret) {
	return hashTableGet(&vm->module->globals, copyString(vm, fname, strlen(fname)), ret);
}

bool blRise(VM *vm, const char* cls, const char *err, ...) {
	sbuf_clear(&vm->stacktrace);

	Value excVal;
	if(!(blGetGlobal(vm, cls, &excVal) && IS_CLASS(excVal))) {
		return false;
	}

	ObjInstance *excInst = newInstance(vm, AS_CLASS(excVal));

	if(err != NULL) {
		push(vm, OBJ_VAL(excInst));

		char errStr[1024] = {0};
		va_list args;
		va_start(args, err);
		vsnprintf(errStr, sizeof(errStr) - 1, err, args);
		va_end(args);

		blSetField(vm, excInst, "err", OBJ_VAL(copyString(vm, errStr, strlen(errStr))));
		pop(vm);
	}

	vm->exception = (Obj*) excInst;

	return true;
}
