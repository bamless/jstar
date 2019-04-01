#ifndef VM_H
#define VM_H

#include "value.h"
#include "object.h"
#include "compiler.h"
#include "hashtable.h"

#include "util/stringbuf.h"

#include <stdlib.h>
#include <stdint.h>

#define FRAME_SZ 1000                             // Max stack depth
#define STACK_SZ (FRAME_SZ + 1) * (UINT8_MAX + 1) // We have at most UINT8_MAX+1 local var per frame
#define INIT_GC  1024 * 1024 * 20                 // 20MiB

#define HANDLER_MAX 10 // Max number of nested TryExcepts

typedef enum {
	VM_EVAL_SUCCSESS, // The VM successfully executed the code
	VM_SYNTAX_ERR,    // A syntax error has been encountered in parsing
	VM_COMPILE_ERR,   // An error has been encountered during compilation
	VM_RUNTIME_ERR,   // An unhandled exception has reached the top of the stack
} EvalResult;

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

typedef struct Frame {
	uint8_t *ip;                   // Instruction pointer
	Value *stack;                  // Base of stack for current frame
	ObjClosure *closure;           // The function associated with the frame
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
	Value stack[STACK_SZ], *sp;

	Frame frames[FRAME_SZ];
	int frameCount;

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

BlangVM *blNewVM();
void blFreeVM(BlangVM *vm);

EvalResult blEvaluate(BlangVM *vm, const char *fpath, const char *src);
EvalResult blEvaluateModule(BlangVM *vm, const char *fpath, const char *name, const char *src);

void  push(BlangVM *vm, Value v);
Value pop(BlangVM *vm);

void blInitCommandLineArgs(int argc, const char **argv);
void blAddImportPath(BlangVM *vm, const char *path);

#define peek(vm)     ((vm)->sp[-1])
#define peek2(vm)    ((vm)->sp[-2])
#define peekn(vm, n) ((vm)->sp[-(n + 1)])

#endif
