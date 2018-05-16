#include "native.h"
#include "vm.h"

#include <stdarg.h>
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

void blRuntimeError(VM *vm, const char *format, ...) {
	fprintf(stderr, "Traceback:\n");

	for(int i = 0; i < vm->frameCount; i++) {
		Frame *frame = &vm->frames[i];
		ObjFunction *func = frame->fn;
		size_t istr = frame->ip - func->chunk.code - 1;
		fprintf(stderr, "    [line:%d] ", getBytecodeSrcLine(&func->chunk, istr));

		fprintf(stderr, "module %s in ", func->module->name->data);

		if(func->name != NULL) {
			fprintf(stderr, "%s()\n", func->name->data);
		} else {
			fprintf(stderr, "<main>\n");
		}

	}

	va_list args;
	va_start(args, format);
	vfprintf(stderr, format, args);
	va_end(args);
	fprintf(stderr, "\n");

	vm->error = true;
}
