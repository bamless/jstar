#include "native.h"
#include "vm.h"

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
