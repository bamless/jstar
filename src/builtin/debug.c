#include "debug.h"
#include "chunk.h"
#include "vm.h"

#include "debug/disassemble.h"

#include <stdio.h>

NATIVE(bl_printStack) {
	for(Value *v = vm->stack; v < vm->sp; v++) {
		printf("[");
		printValue(*v);
		printf("]");
	}
	printf("$\n");

	BL_RETURN(NULL_VAL);
}

NATIVE(bl_dis) {
	if(!IS_OBJ(args[1]) || !(IS_CLOSURE(args[1]) ||
			IS_NATIVE(args[1]) || IS_BOUND_METHOD(args[1]))) {
		BL_RAISE_EXCEPTION(vm, "InvalidArgException",
		 	"Argument to dis must be a function object.");
	}

	Value func = args[1];
	if(IS_BOUND_METHOD(func)) {
		func = OBJ_VAL(AS_BOUND_METHOD(func)->method);
	}

	if(!IS_NATIVE(func)) {
		Chunk *c = &AS_CLOSURE(func)->fn->chunk;
		disassembleChunk(c);
	} else {
		printf("Native implementation\n");
	}

	BL_RETURN(NULL_VAL);
}
