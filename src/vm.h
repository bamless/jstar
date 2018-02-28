#ifndef VM_H
#define VM_H

#include "value.h"
#include "memory.h"
#include "hashtable.h"

#include <stdint.h>

#define FRAME_SZ 1024
#define STACK_SZ FRAME_SZ * UINT8_MAX

typedef struct Frame {
	uint8_t *ip;
	Value *stack;
	ObjFunction *fn;
} Frame;

typedef struct VM {
	MemManager mem;

	Value stack[STACK_SZ];
	Value *sp;

	Frame frames[FRAME_SZ];
	int frameCount;

	HashTable globals;
	HashTable strings;
} VM;

void initVM(VM *vm);
void freeVM(VM *vm);

#endif
