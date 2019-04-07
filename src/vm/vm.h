#ifndef VM_H
#define VM_H

#include "value.h"
#include "object.h"
#include "compiler.h"
#include "hashtable.h"
#include "util.h"

#include <stdlib.h>
#include <stdint.h>

#define RECURSION_LIMIT 10000                     // After RECURSION_LIMIT calls, StackOverflowException will be thrown

#define FRAME_SZ 1000                             // Max stack depth
#define STACK_SZ FRAME_SZ * (UINT8_MAX + 1)       // We have at most UINT8_MAX+1 local var per frame
#define INIT_GC  1024 * 1024 * 20                 // 20MiB

#define HANDLER_MAX 10 // Max number of nested TryExcepts

typedef enum HandlerType {
	HANDLER_ENSURE,
	HANDLER_EXCEPT
} HandlerType;

// TryExcept Handler
typedef struct Handler {
	HandlerType type; // The type of the handler block
	uint8_t *handler; // The start of except handler
	Value *savesp;    // Stack pointer to restore when handling exceptions
} Handler;

typedef struct {
	ObjType type;
	union {
		ObjClosure *closure;
		ObjNative *native;
	};
} Function;

typedef struct Frame {
	uint8_t *ip;                   // Instruction pointer
	Value *stack;                  // Base of stack for current frame
	Function fn;                   // The function associated with the frame
	Handler handlers[HANDLER_MAX]; // Exception handlers
	uint8_t handlerc;              // Exception handlers count
} Frame;

typedef struct BlangVM {
	// Paths searched for import
	ObjList *importpaths;

	// Built in classes
	ObjClass *clsClass;
	ObjClass *objClass;
	ObjClass *strClass;
	ObjClass *boolClass;
	ObjClass *lstClass;
	ObjClass *numClass;
	ObjClass *funClass;
	ObjClass *modClass;
	ObjClass *nullClass;
	ObjClass *stClass;
	ObjClass *tupClass;
	ObjClass *rangeClass;

	// Current exception
	ObjInstance *exception;

	// Current VM compiler
	Compiler *currCompiler;

	// Constant strings needed by compiler and runtime
	ObjString *ctor;
	ObjString *stField;

	// The empty tuple (singleton)
	ObjTuple *emptyTup;

	// Names of overloadable operator's methods
	ObjString *add, *sub, *mul, *div, *mod, *get, *set;
	ObjString *radd, *rsub, *rmul, *rdiv, *rmod;
	ObjString *lt, *le, *gt, *ge, *eq, *neg;

	//loaded modules
	HashTable modules;
	//current module
	ObjModule *module;
	//core module
	ObjModule *core;

	// VM program stack
	size_t stackSz;
	Value *stack, *sp;

	int frameSz;
	Frame *frames;
	int frameCount;

	Value *apiStack;

	// Constant strings pool, all strings are interned
	HashTable strings;

	// Linked list of all open upvalues
	ObjUpvalue *upvalues;

	// Memory management
	Obj *objects;

	bool disableGC;
	size_t allocated;
	size_t nextGC;

	Obj **reachedStack;
	size_t reachedCapacity, reachedCount;
} BlangVM;

static inline void push(BlangVM *vm, Value v) {
	*vm->sp++ = v;
}

static inline Value pop(BlangVM *vm) {
	return *--vm->sp;
}

static inline Value apiStackSlot(BlangVM *vm, int slot) {
    assert(vm->sp - slot > vm->apiStack, "API stack slot would be negative");
    assert(vm->apiStack + slot < vm->sp, "API stack overflow");
    if(slot < 0) return vm->sp[slot];
    else return vm->apiStack[slot];
}

#define peek(vm)     ((vm)->sp[-1])
#define peek2(vm)    ((vm)->sp[-2])
#define peekn(vm, n) ((vm)->sp[-(n + 1)])

#endif
