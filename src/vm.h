#ifndef VM_H
#define VM_H

#include "value.h"
#include "memory.h"
#include "hashtable.h"
#include "compiler.h"

#include <stdint.h>

#define FRAME_SZ 1000 // Max stack depth
#define STACK_SZ FRAME_SZ * UINT8_MAX // we have at most UINT8_MAX local var per stack

typedef struct Frame {
	uint8_t *ip;
	Value *stack;
	ObjFunction *fn;
} Frame;

typedef struct VM {
	MemManager mem;
	Compiler *currCompiler;

	Value stack[STACK_SZ];
	Value *sp;

	Frame frames[FRAME_SZ];
	int frameCount;

	HashTable globals;
	HashTable strings;
} VM;

void initVM(VM *vm);
void freeVM(VM *vm);

#define push(vm, v) (*(vm)->sp = (v), (vm)->sp++)
#define pop(vm) ((vm)->sp--)

#endif
