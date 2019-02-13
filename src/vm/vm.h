#ifndef VM_H
#define VM_H

#include "value.h"
#include "object.h"
#include "compiler.h"
#include "hashtable.h"

#include "util/stringbuf.h"

#include <stdlib.h>
#include <stdint.h>

#define CTOR_STR "new"
#define THIS_STR "this"

#define FRAME_SZ 1000                       // Max stack depth
#define STACK_SZ FRAME_SZ * (UINT8_MAX + 1) // We have at most UINT8_MAX+1 local var per frame
#define INIT_GC  1024 * 1024 * 20           // 20MiB

#define HADLER_MAX 5

typedef enum {
	VM_EVAL_SUCCSESS,
	VM_SYNTAX_ERR,
	VM_COMPILE_ERR,
	VM_RUNTIME_ERR,
	VM_GENERIC_ERR
} EvalResult;

typedef struct Handler {
	uint8_t *handler;
	Value *savesp;
} Handler;

typedef struct Frame {
	uint8_t *ip;                  // Instruction pointer
	Value *stack;                 // Base of stack for current frame
	ObjFunction *fn;              // The function associated with the frame
	Handler handlers[HADLER_MAX]; // Exception handlers
	uint8_t handlerc;             // Exception handlers count
} Frame;

typedef struct BlangVM {
	// paths searched for import
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
	ObjClass *excClass;

	// Current exception
	Obj *exception;
	// Stack trace of current expression
	StringBuffer stacktrace;
	int lastTracedFrame;

	// Current VM compiler
	Compiler *currCompiler;

	// Constant strings
	ObjString *ctor;

	// Names of overloadable operator's methods
	ObjString *add, *sub, *mul, *div, *mod, *get, *set, *lt, *le, *gt, *ge;
	ObjString *radd, *rsub, *rmul, *rdiv, *rmod;

	//loaded modules
	HashTable modules;
	//current module
	ObjModule *module;

	// VM program stack
	Value stack[STACK_SZ], *sp;

	Frame frames[FRAME_SZ];
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
