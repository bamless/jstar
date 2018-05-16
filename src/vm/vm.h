#ifndef VM_H
#define VM_H

#include "value.h"
#include "object.h"
#include "compiler.h"
#include "hashtable.h"

#include <stdlib.h>
#include <stdint.h>

#define CTOR "new"
#define THIS "this"

#define FRAME_SZ 1000                       // Max stack depth
#define STACK_SZ FRAME_SZ * (UINT8_MAX + 1) // We have at most UINT8_MAX+1 local var per frame
#define INIT_GC 1024 * 1024                 // 1MiB

typedef enum {
	VM_EVAL_SUCCSESS,
	VM_SYNTAX_ERR,
	VM_COMPILE_ERR,
	VM_RUNTIME_ERR,
	VM_GENERIC_ERR
} EvalResult;

typedef struct Frame {
	uint8_t *ip;       // Instruction pointer
	Value *stack;      // Base of stack for current frame
	ObjFunction *fn;   // The function associated with the frame
} Frame;

typedef struct VM {
	bool error;
	// Current VM compiler
	Compiler *currCompiler;
	ObjString *ctor;

	//loaded modules
	HashTable modules;
	//current module
	ObjModule *module;

	// VM program stack
	Value *stack, *sp, *stackend;

	Frame *frames;
	int frameCount;

	// Globals and constant strings pool
	HashTable strings;

	// Memory management
	Obj *objects;

	bool disableGC;
	size_t allocated;
	size_t nextGC;

	Obj **reachedStack;
	size_t reachedCapacity, reachedCount;
} VM;

void initVM(VM *vm);
void freeVM(VM *vm);

EvalResult evaluate(VM *vm, const char *src);
EvalResult evaluateModule(VM *vm, const char *name, const char *src);

void push(VM *vm, Value v);
Value pop(VM *vm);

#define peek(vm)     ((vm)->sp[-1])
#define peek2(vm)    ((vm)->sp[-2])
#define peekn(vm, n) ((vm)->sp[-(n + 1)])

#endif
