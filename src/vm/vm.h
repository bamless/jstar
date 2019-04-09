#ifndef VM_H
#define VM_H

#include "value.h"
#include "object.h"
#include "compiler.h"
#include "hashtable.h"
#include "util.h"

#include <stdlib.h>
#include <stdint.h>

#define RECURSION_LIMIT 10000               // After RECURSION_LIMIT calls, StackOverflowException will be thrown

#define FRAME_SZ 1000                       // Starting frame size
#define STACK_SZ FRAME_SZ * (UINT8_MAX + 1) // We have at most UINT8_MAX+1 local var per frame
#define INIT_GC  1024 * 1024 * 20           // 20MiB

#define HANDLER_MAX 10                      // Max number of nested try/except/ensure

typedef enum HandlerType {
	HANDLER_ENSURE,
	HANDLER_EXCEPT
} HandlerType;

// This stores the info needed to jump
// to handler code and to restore the
// VM state when handling exceptions
typedef struct Handler {
	HandlerType type; // The type of the handler block
	uint8_t *handler; // The start of except (or ensure) handler
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
	Function fn;                   // The function associated with the frame (a Native or a Closure)
	Handler handlers[HANDLER_MAX]; // Exception handlers
	uint8_t handlerc;              // Exception handlers count
} Frame;

// The Blang VM. This struct stores all the 
// state needed to execute Blang code.
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

	// Current VM compiler
	Compiler *currCompiler;

	// Constant strings needed by compiler and runtime
	ObjString *ctor;
	ObjString *stField;

	// Names of overloadable operator's methods
	ObjString *add, *sub, *mul, *div, *mod, *get, *set;
	ObjString *radd, *rsub, *rmul, *rdiv, *rmod;
	ObjString *lt, *le, *gt, *ge, *eq, *neg;

	// The empty tuple (singleton)
	ObjTuple *emptyTup;

	//loaded modules
	HashTable modules;
	//current module and core module
	ObjModule *module, *core;

	// VM program stack
	size_t stackSz;
	Value *stack, *sp;

	int frameSz;
	Frame *frames;
	int frameCount;

	// Stack used during Native function calls
	Value *apiStack;

	// Constant string pool, for interned strings
	HashTable strings;

	// Linked list of all open upvalues
	ObjUpvalue *upvalues;

	// Memory management

	// Linked list of all allocated objects (used in 
	// the sweep phase of GC to free unreached objects)
	Obj *objects;

	bool disableGC;
	size_t allocated; // Bytes currently allocated
	size_t nextGC;    // Bytes to which the next GC will be triggered

	// Stack used to recursevely reach all the fields of reached objects
	Obj **reachedStack;
	size_t reachedCapacity, reachedCount;
} BlangVM;

static inline void push(BlangVM *vm, Value v) {
	*vm->sp++ = v;
}

static inline Value pop(BlangVM *vm) {
	return *--vm->sp;
}

// Get the value at API stack slot "slot"
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
