#include "vm.h"

#include <stdlib.h>

#define INIT_GC 1024 * 1024 // 1MiB


#ifdef DBG_PRINT_GC
#include <stdio.h>
#endif

static void reset(VM *vm) {
	vm->sp = vm->stack;
	vm->frameCount = 0;
}

void initVM(VM *vm) {
	vm->currCompiler = NULL;

	vm->stack  = malloc(sizeof(Value) * STACK_SZ);
	vm->frames = malloc(sizeof(Frame) * FRAME_SZ);

	reset(vm);

	initHashTable(&vm->globals);
	initHashTable(&vm->strings);

	vm->nextGC = INIT_GC;
	vm->objects = NULL;
	vm->disableGC = false;

	vm->allocated = 0;
	vm->reachedStack = NULL;
	vm->reachedCapacity = 0;
	vm->reachedCount = 0;
}

void freeVM(VM *vm) {
	reset(vm);

	freeHashTable(&vm->globals);
	freeHashTable(&vm->strings);
	freeObjects(vm);

	free(vm->stack);
	free(vm->frames);

#ifdef DBG_PRINT_GC
	printf("Allocated at exit: %lu bytes\n", vm->allocated);
#endif
}
