#include "vm.h"

void initVM(VM *vm) {
	initMemoryManager(&vm->mem, vm);
	vm->sp = vm->stack;
	vm->frameCount = 0;
	initHashTable(&vm->globals);
	initHashTable(&vm->strings);
}

static void reset(VM *vm) {
	vm->sp = vm->stack;
	vm->frameCount = 0;
}

void freeVM(VM *vm) {
	reset(vm);
	freeMemoryManager(&vm->mem);
	freeHashTable(&vm->globals);
	freeHashTable(&vm->strings);
}
