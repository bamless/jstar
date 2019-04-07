#include "vm.h"
#include "opcode.h"
#include "import.h"
#include "memory.h"
#include "modules.h"
#include "core.h"
#include "options.h"
#include "sys.h"
#include "blang.h"

#include "debug/disassemble.h"

#include "parse/parser.h"
#include "parse/ast.h"

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <float.h>
#include <math.h>
#include <limits.h>
#include <string.h>

// Enumeration encoding the cause of the stack
// unwinding, used during unwinding to correctly
// handle the execution of except/ensure handlers
typedef enum UnwindCause {
	CAUSE_EXCEPT,
	CAUSE_RETURN
} UnwindCause;

static bool unwindStack(BlangVM *vm, int depth);

static void reset(BlangVM *vm) {
	vm->sp = vm->stack;
	vm->apiStack = vm->stack;
	vm->frameCount = 0;
	vm->exception = NULL;
}

BlangVM *blNewVM() {
	BlangVM *vm = calloc(1, sizeof(*vm));

	vm->stackSz = STACK_SZ;
	vm->stack = malloc(sizeof(Value) * STACK_SZ);
	
	vm->frameSz = FRAME_SZ;
	vm->frames = malloc(sizeof(Frame) * FRAME_SZ);

	reset(vm);

	initHashTable(&vm->modules);
	initHashTable(&vm->strings);

	// init GC
	vm->nextGC = INIT_GC;
	vm->objects = NULL;
	vm->disableGC = false;

	vm->allocated = 0;
	vm->reachedStack = NULL;
	vm->reachedCapacity = 0;
	vm->reachedCount = 0;

	// Create constants strings
	vm->ctor     = copyString(vm, CTOR_STR, strlen(CTOR_STR), true);
	vm->stField  = copyString(vm, "stacktrace", 10, true);

	vm->add = copyString(vm, "__add__", 7, true);
	vm->sub = copyString(vm, "__sub__", 7, true);
	vm->mul = copyString(vm, "__mul__", 7, true);
	vm->div = copyString(vm, "__div__", 7, true);
	vm->mod = copyString(vm, "__mod__", 7, true);
	vm->get = copyString(vm, "__get__", 7, true);
	vm->set = copyString(vm, "__set__", 7, true);

	vm->radd = copyString(vm, "__radd__", 8, true);
	vm->rsub = copyString(vm, "__rsub__", 8, true);
	vm->rmul = copyString(vm, "__rmul__", 8, true);
	vm->rdiv = copyString(vm, "__rdiv__", 8, true);
	vm->rmod = copyString(vm, "__rmod__", 8, true);

	vm->lt  = copyString(vm, "__lt__", 6, true);
	vm->le  = copyString(vm, "__le__", 6, true);
	vm->gt  = copyString(vm, "__gt__", 6, true);
	vm->ge  = copyString(vm, "__ge__", 6, true);
	vm->eq  = copyString(vm, "__eq__", 6, true);

	vm->neg = copyString(vm, "__neg__", 7, true);

	// Bootstrap the core module
	initCoreLibrary(vm);

	// This is called after initCoreLibrary in order to correctly assign the
	// List class to the object since classes are created during initialization
	vm->importpaths = newList(vm, 8);
	vm->emptyTup = newTuple(vm, 0);

	return vm;
}

void blFreeVM(BlangVM *vm) {
	reset(vm);

	free(vm->stack);
	free(vm->frames);

	freeHashTable(&vm->strings);
	freeHashTable(&vm->modules);
	freeObjects(vm);

#ifdef DBG_PRINT_GC
	printf("Allocated at exit: %lu bytes.\n", vm->allocated);
#endif

	free(vm);
}

static Frame *getFrame(BlangVM *vm, Callable *c) {
	if(vm->frameCount + 1 == vm->frameSz) {
		vm->frameSz *= 2;
		vm->frames = realloc(vm->frames, sizeof(Frame) * vm->frameSz);
	}

	Frame *callFrame = &vm->frames[vm->frameCount++];
	callFrame->stack = vm->sp - (c->argsCount + 1);
	callFrame->handlerc = 0;
	if(c->vararg) callFrame->stack--;

	return callFrame;
}

static void appendCallFrame(BlangVM *vm, ObjClosure *closure) {
	Frame *callFrame = getFrame(vm, &closure->fn->c);
	callFrame->fn.type = OBJ_CLOSURE;
	callFrame->fn.closure = closure;
	callFrame->ip = closure->fn->chunk.code;
}

static void appendNativeFrame(BlangVM *vm, ObjNative *native) {
	Frame *callFrame = getFrame(vm, &native->c);
	callFrame->fn.type = OBJ_NATIVE;
	callFrame->fn.native = native;
	callFrame->ip = NULL;
}

static void ensureStack(BlangVM *vm, size_t needed) {
	if(vm->sp + needed < vm->stack + vm->stackSz) return;

	Value *oldStack = vm->stack;

	vm->stackSz = powerOf2Ceil(vm->stackSz);
	vm->stack = realloc(vm->stack, sizeof(Value) * vm->stackSz);

	if(vm->stack != oldStack) {
		if(vm->apiStack >= vm->stack && vm->apiStack <= vm->sp) {
			vm->apiStack = vm->stack + (vm->apiStack - oldStack);
		}

		for(int i = 0; i < vm->frameCount; i++) {
			Frame *frame = &vm->frames[i];
			frame->stack = vm->stack + (frame->stack - oldStack);
			for(int i = 0; i < frame->handlerc; i++) {
				Handler *h = &frame->handlers[i];
				h->savesp = vm->stack + (h->savesp - oldStack);
			}
		}

		ObjUpvalue *upvalue = vm->upvalues;
		while(upvalue) {
			upvalue->addr = vm->stack + (upvalue->addr - oldStack);
			upvalue = upvalue->next;
		}

		vm->sp = vm->stack + (vm->sp - oldStack);
	}
}

static ObjClass *getClass(BlangVM *vm, Value v) {
  	if(IS_NUM(v)) {
		return vm->numClass;
	} else if(IS_BOOL(v)) {
		return vm->boolClass;
	} else if(IS_OBJ(v)) {
		return AS_OBJ(v)->cls;
	} else {
		return vm->nullClass;
	}
}

static bool isNonInstantiableBuiltin(BlangVM *vm, ObjClass *cls) {
	return cls == vm->numClass  || 
	       cls == vm->strClass  ||
           cls == vm->boolClass ||
	       cls == vm->nullClass ||
		   cls == vm->funClass  ||
		   cls == vm->modClass  ||
		   cls == vm->stClass   ||
		   cls == vm->tupClass;
}

static bool isInstatiableBuiltin(BlangVM *vm, ObjClass *cls) {
	return cls == vm->lstClass || cls == vm->rangeClass;
}

static bool isInstance(BlangVM *vm, Value i, ObjClass *cls) {
	for(ObjClass *c = getClass(vm, i); c != NULL; c = c->superCls) {
		if(c == cls) {
			return true;
		}
	}
	return false;
}

static bool isInt(double n) {
	return (int64_t) n == n;
}

static void createClass(BlangVM *vm, ObjString *name, ObjClass *superCls) {
	ObjClass *cls = newClass(vm, name, superCls);
	hashTableMerge(&cls->methods, &superCls->methods);
	push(vm, OBJ_VAL(cls));
}

static ObjUpvalue *captureUpvalue(BlangVM *vm, Value *addr) {
	if(vm->upvalues == NULL) {
		vm->upvalues = newUpvalue(vm, addr);
		return vm->upvalues;
	}

	ObjUpvalue *prev = NULL;
	ObjUpvalue *upvalue = vm->upvalues;

	while(upvalue != NULL && upvalue->addr > addr) {
		prev = upvalue;
		upvalue = upvalue->next;
	}

	if(upvalue != NULL && upvalue->addr == addr)
		return upvalue;

	ObjUpvalue *createdUpvalue = newUpvalue(vm, addr);
	if(prev == NULL)
		vm->upvalues = createdUpvalue;
	else
		upvalue->next = createdUpvalue;

	createdUpvalue->next = upvalue;
	return createdUpvalue;
}

static void closeUpvalues(BlangVM *vm, Value *last) {
	while(vm->upvalues != NULL && vm->upvalues->addr >= last) {
		ObjUpvalue *upvalue = vm->upvalues;

		upvalue->closed = *upvalue->addr;
		upvalue->addr = &upvalue->closed;

		vm->upvalues = upvalue->next;
	}
}

static void packVarargs(BlangVM *vm, uint8_t count) {
	ObjTuple *args = newTuple(vm, count);
	for(int i = count - 1; i >= 0; i--) {
		args->arr[i] = pop(vm);
	}
	push(vm, OBJ_VAL(args));
}

static bool adjustArguments(BlangVM *vm, Callable *c, uint8_t argc) {
	if(c->defaultc != 0) {
		uint8_t most  = c->argsCount;
		uint8_t least = most - c->defaultc;

		if((!c->vararg && argc > most) || argc < least) {
			const char *mname = c->module->name->data;
			const char *name = c->name->data;

			blRaise(vm, "TypeException", "Function `%s.%s` takes at %s %d args, %d supplied.", 
				mname, name, argc > most ? "most" : "least", argc > most ? most : least, argc);
			
			return false;
		}
	
		// push remaining args taking the default value
		for(uint8_t i = argc - least; i < c->defaultc; i++) {
			push(vm, c->defaults[i]);
		}
		
		if(c->vararg) packVarargs(vm, argc > most ? argc - most : 0);
	} else if(c->vararg) {
		if(argc < c->argsCount) {
			const char *mname = c->module->name->data;
			const char *name = c->name->data;

			blRaise(vm, "TypeException", "Function `%s.%s` takes at least %d "
						"args, %d supplied.", mname, name, c->argsCount, argc);

			return false;
		}
		packVarargs(vm, argc - c->argsCount);
	} else if(c->argsCount != argc) {
		const char *mname = c->module->name->data;
		const char *name = c->name->data;

		blRaise(vm, "TypeException", "Function `%s.%s` takes exactly %d "
					"args, %d supplied.", mname, name, c->argsCount, argc);

		return false;
	}

	return true;
}

static bool callFunction(BlangVM *vm, ObjClosure *closure, uint8_t argc) {
	if(vm->frameCount + 1 == RECURSION_LIMIT) {
		blRaise(vm, "StackOverflowException", NULL);
		return false;
	}

	if(!adjustArguments(vm, &closure->fn->c, argc)) {
		return false;
	}

	// TODO: modify compiler to track actual usage of stack so
	// we can allocate the right amount of memory rather than a
	// worst case bound
	ensureStack(vm, UINT8_MAX);
	appendCallFrame(vm, closure);
	
	vm->module = closure->fn->c.module;

	return true;
}

static bool callNative(BlangVM *vm, ObjNative *native, uint8_t argc) {
	if(vm->frameCount + 1 == RECURSION_LIMIT) {
		blRaise(vm, "StackOverflowException", NULL);
		return false;
	}

	if(!adjustArguments(vm, &native->c, argc)) {
		return false;
	}

	ensureStack(vm, MIN_NATIVE_STACK_SZ);
	appendNativeFrame(vm, native);

	ObjModule *oldModule = vm->module;

	vm->module = native->c.module;
	vm->apiStack = vm->frames[vm->frameCount - 1].stack;

	if(!native->fn(vm)) {
		assert(vm->exception != NULL, "Native failed without setting exception");
		return false;
	}

	Value ret = pop(vm);

	vm->frameCount--;
	vm->sp = vm->apiStack;
	vm->module = oldModule;

	push(vm, ret);

	return true;
}

static bool callValue(BlangVM *vm, Value callee, uint8_t argc) {
	if(IS_OBJ(callee)) {
		switch(OBJ_TYPE(callee)) {
		case OBJ_CLOSURE:
			return callFunction(vm, AS_CLOSURE(callee), argc);
		case OBJ_NATIVE:
			return callNative(vm, AS_NATIVE(callee), argc);
		case OBJ_BOUND_METHOD: {
			ObjBoundMethod *m = AS_BOUND_METHOD(callee);
			vm->sp[-argc - 1] = m->bound;
			return m->method->type == OBJ_CLOSURE ?
			        callFunction(vm, (ObjClosure*)m->method, argc) :
			        callNative(vm, (ObjNative*)m->method, argc);
		}
		case OBJ_CLASS: {
			ObjClass *cls = AS_CLASS(callee);

			if(isNonInstantiableBuiltin(vm, cls)) {
				blRaise(vm, "Exception", "class %s can't be directly instatiated", cls->name->data);
				return false;
			}

			vm->sp[-argc - 1] = isInstatiableBuiltin(vm, cls) ? NULL_VAL : OBJ_VAL(newInstance(vm, cls));

			Value ctor;
			if(hashTableGet(&cls->methods, vm->ctor, &ctor)) {
				return callValue(vm, ctor, argc);
			} else if(argc != 0) {
				blRaise(vm, "TypeException", "Function %s.new() Expected 0 "
				    "args, but instead `%d` supplied.", cls->name->data, argc);
				return false;
			}

			return true;
		}
		default: break;
		}
	}

	ObjClass *cls = getClass(vm, callee);
	blRaise(vm, "TypeException", "Object %s is not a callable.", cls->name->data);
	return false;
}

static bool invokeMethod(BlangVM *vm, ObjClass *cls, ObjString *name, uint8_t argc) {
	Value method;
	if(!hashTableGet(&cls->methods, name, &method)) {
		blRaise(vm, "MethodException", "Method %s.%s() doesn't "
			                "exists", cls->name->data, name->data);
		return false;
	}

	return callValue(vm, method, argc);
}

static bool invokeFromValue(BlangVM *vm, ObjString *name, uint8_t argc) {
	Value val = peekn(vm, argc);
	if(IS_OBJ(val)) {
		switch(OBJ_TYPE(val)) {
		case OBJ_INST: {
			ObjInstance *inst = AS_INSTANCE(val);

			// Check if field shadows a method
			Value f;
			if(hashTableGet(&inst->fields, name, &f)) {
				return callValue(vm, f, argc);
			}

			return invokeMethod(vm, inst->base.cls, name, argc);
		}
		case OBJ_MODULE: {
			ObjModule *mod = AS_MODULE(val);

			Value func;
			// check if method shadows a function in the module
			if(hashTableGet(&vm->modClass->methods, name, &func)) {
				return callValue(vm, func, argc);
			}

			if(!hashTableGet(&mod->globals, name, &func)) {
				blRaise(vm, "NameException", "Name `%s` is not defined "
					        "in module %s.", name->data, mod->name->data);
				return false;
			}

			return callValue(vm, func, argc);
		}
		default: {
			Obj *o = AS_OBJ(val);
			return invokeMethod(vm, o->cls, name, argc);
		}
		}
	}

	// if builtin type get the class and try to invoke
	ObjClass *cls = getClass(vm, val);
	return invokeMethod(vm, cls, name, argc);
}

bool getFieldFromValue(BlangVM *vm, Value val, ObjString *name) {
	if(IS_OBJ(val)) {
		switch(OBJ_TYPE(val)) {
		case OBJ_INST: {
			ObjInstance *inst = AS_INSTANCE(val);

			Value v;
			if(!hashTableGet(&inst->fields, name, &v)) {
				//if we didnt find a field try to return bound method
				if(!hashTableGet(&inst->base.cls->methods, name, &v)) {
					blRaise(vm, "FieldException", "Object %s doesn't have "
						"field `%s`.", inst->base.cls->name->data, name->data);
					return false;
				}

				push(vm, OBJ_VAL(newBoundMethod(vm, val, AS_OBJ(v))));
				return true;
			}

			push(vm, v);
			return true;
		}
		case OBJ_MODULE: {
			ObjModule *mod = AS_MODULE(val);

			Value v;
			if(!hashTableGet(&mod->globals, name, &v)) {
				//if we didnt find a global name try to return bound method
				if(!hashTableGet(&mod->base.cls->methods, name, &v)) {
					blRaise(vm, "NameException", "Name `%s` is not "
						"defined in module %s", name->data, mod->name->data);
					return false;
				}

				push(vm, OBJ_VAL(newBoundMethod(vm, val, AS_OBJ(v))));
				return true;
			}

			push(vm, v);
			return true;
		}
		default: break;
		}
	}

	Value v;
	ObjClass *cls = getClass(vm, val);

	if(!hashTableGet(&cls->methods, name, &v)) {
		blRaise(vm, "FieldException", "Object %s doesn't have "
			"field `%s`.", cls->name->data, name->data);
		return false;
	}

	push(vm, OBJ_VAL(newBoundMethod(vm, val, AS_OBJ(v))));
	return true;
}

bool setFieldOfValue(BlangVM *vm, Value val, ObjString *name, Value s) {
	if(IS_OBJ(val)) {
		switch(OBJ_TYPE(val)) {
		case OBJ_INST: {
			ObjInstance *inst = AS_INSTANCE(val);
			hashTablePut(&inst->fields, name, s);
			return true;
		}
		case OBJ_MODULE: {
			ObjModule *mod = AS_MODULE(val);
			hashTablePut(&mod->globals, name, s);
			return true;
		}
		default: break;
		}
	}

	ObjClass *cls = getClass(vm, val);
	blRaise(vm, "FieldException", "Object %s doesn't "
	        "have field `%s`.", cls->name->data, name->data);
	return false;
}

static bool isValTrue(Value val) {
	if(IS_BOOL(val)) return AS_BOOL(val);
	return !IS_NULL(val);
}

static ObjString* stringConcatenate(BlangVM *vm, ObjString *s1, ObjString *s2) {
	size_t length = s1->length + s2->length;
	ObjString *str = allocateString(vm, length);
	memcpy(str->data, s1->data, s1->length);
	memcpy(str->data + s1->length, s2->data, s2->length);
	return str;
}

static bool callBinaryOverload(BlangVM *vm, ObjString *name, ObjString *reverse) {
	ObjClass *cls = getClass(vm, peek2(vm));

	Value op;
	if(hashTableGet(&cls->methods, name, &op)) {
		return callValue(vm, op, 1);
	}

	// swap callee and arg
	Value b = peek(vm);
	vm->sp[-1] = vm->sp[-2];
	vm->sp[-2] = b;

	ObjClass *cls2 = getClass(vm, peek2(vm));

	if(hashTableGet(&cls2->methods, name, &op)) {
		return callValue(vm, op, 1);
	}

	return false;
}

static bool runEval(BlangVM *vm, int depth) {
	register Frame *frame;
	register Value *frameStack;
	register ObjClosure *closure;
	register ObjFunction *fn;
	register uint8_t *ip;

	#define LOAD_FRAME() \
		frame = &vm->frames[vm->frameCount - 1]; \
		frameStack = frame->stack; \
		closure = frame->fn.closure; \
		fn = closure->fn; \
		ip = frame->ip; \

	#define SAVE_FRAME() frame->ip = ip;

	#define NEXT_CODE()  (*ip++)
	#define NEXT_SHORT() (ip += 2, ((uint16_t) ip[-2] << 8) | ip[-1])

	#define GET_CONST()  (fn->chunk.consts.arr[NEXT_SHORT()])
	#define GET_STRING() (AS_STRING(GET_CONST()))

	#define BINARY(type, op, overload, reverse) do { \
		if(IS_NUM(peek(vm)) && IS_NUM(peek2(vm))) { \
			double b = AS_NUM(pop(vm)); \
			double a = AS_NUM(pop(vm)); \
			push(vm, type(a op b)); \
		} else { \
			SAVE_FRAME(); \
			if(!callBinaryOverload(vm, overload, reverse)) {   \
				LOAD_FRAME(); \
				ObjString *t1 = getClass(vm, peek(vm))->name;  \
				ObjString *t2 = getClass(vm, peek2(vm))->name; \
				blRaise(vm, "TypeException", "Operator %s not defined "  \
				            "for types %s, %s", #op, t1->data, t2->data); \
				UNWIND_STACK(vm); \
			} \
			LOAD_FRAME(); \
		} \
	} while(0)

	#define UNWIND_HANDLER(h, cause, ret) do { \
		frame->ip = h->handler; \
		vm->sp = h->savesp; \
		closeUpvalues(vm, vm->sp - 1); \
		push(vm, cause); \
		push(vm, ret); \
	} while(0)

	#define UNWIND_STACK(vm) do { \
		SAVE_FRAME() \
		if(!unwindStack(vm, depth)) { \
			return false; \
		} \
		LOAD_FRAME(); \
		DISPATCH(); \
	} while(0)

	#ifdef DBG_PRINT_EXEC
		#define PRINT_DBG_STACK() \
			printf("     "); \
			for(Value *v = vm->stack; v < vm->sp; v++) { \
				printf("["); \
				printValue(*v); \
				printf("]"); \
			} \
			printf("$\n"); \
			disassembleIstr(&fn->chunk, (size_t) (ip - fn->chunk.code));
	#else
		#define PRINT_DBG_STACK()
	#endif

	#if defined(USE_COMPUTED_GOTOS) && !defined(_MSC_VER)
		//import jumptable
		#include "opcode_jmp_table.h"

		#define TARGET(op) \
			TARGET_##op \

		#define DISPATCH() do { \
			PRINT_DBG_STACK() \
			goto *opJmpTable[(op = NEXT_CODE())]; \
		} while(0)

		#define DECODE(op) DISPATCH();

		#define GOTO(op) goto TARGET(op)

	#else

		#define TARGET(op) op: case op
		#define DISPATCH() goto decode
		#define DECODE(op) \
		decode: \
			PRINT_DBG_STACK(); \
			switch((op = NEXT_CODE()))

		#define GOTO(op) goto op

	#endif

	LOAD_FRAME();

	uint8_t op;
	DECODE(op) {

	TARGET(OP_ADD): {
		if(IS_NUM(peek(vm)) && IS_NUM(peek2(vm))) {
			double b = AS_NUM(pop(vm));
			double a = AS_NUM(pop(vm));
			push(vm, NUM_VAL(a + b));
		} else if(IS_STRING(peek(vm)) && IS_STRING(peek2(vm))) {
			ObjString *conc = stringConcatenate(vm, AS_STRING(peek2(vm)),
			                                        AS_STRING(peek(vm)));

			pop(vm);
			pop(vm);

			push(vm, OBJ_VAL(conc));
		} else {
			SAVE_FRAME();
			if(!callBinaryOverload(vm, vm->add, vm->radd)) {
				LOAD_FRAME();
				ObjString *t1 = getClass(vm, peek(vm))->name;
				ObjString *t2 = getClass(vm, peek2(vm))->name;

				blRaise(vm, "TypeException", "Operator + not defined"
				            " for types %s, %s", t1->data, t2->data);
				UNWIND_STACK(vm);
			}
			LOAD_FRAME();
		}
		DISPATCH();
	}
	TARGET(OP_DIV): {
		if(IS_NUM(peek(vm)) && IS_NUM(peek2(vm))) {
			double b = AS_NUM(pop(vm));
			double a = AS_NUM(pop(vm));

			if(b == 0) {
				blRaise(vm, "DivisionByZeroException", "Division by zero.");
				UNWIND_STACK(vm);
			}

			push(vm, NUM_VAL(a / b));
		} else {
			SAVE_FRAME();
			if(!callBinaryOverload(vm, vm->div, vm->rdiv)) {
				LOAD_FRAME();
				ObjString *t1 = getClass(vm, peek(vm))->name;
				ObjString *t2 = getClass(vm, peek2(vm))->name;

				blRaise(vm, "TypeException", "Operator / not defined"
							" for types %s, %s", t1->data, t2->data);
				UNWIND_STACK(vm);
			}
			LOAD_FRAME();
		}

		DISPATCH();
	}
	TARGET(OP_MOD): {
		if(IS_NUM(peek(vm)) && IS_NUM(peek2(vm))) {
			double b = AS_NUM(pop(vm));
			double a = AS_NUM(pop(vm));

			if(b == 0) {
				blRaise(vm, "DivisionByZeroException", "Modulo by zero.");
				UNWIND_STACK(vm);
			}

			push(vm, NUM_VAL(fmod(a, b)));
		} else {
			SAVE_FRAME();
			if(!callBinaryOverload(vm, vm->mod, vm->rmod)) {
				ObjString *t1 = getClass(vm, peek(vm))->name;
				ObjString *t2 = getClass(vm, peek2(vm))->name;

				blRaise(vm, "TypeException", "Operator %% not defined"
							" for types %s, %s", t1->data, t2->data);
				UNWIND_STACK(vm);
			}
			LOAD_FRAME();
		}

		DISPATCH();
	}
	TARGET(OP_SUB): BINARY(NUM_VAL,  -,  vm->sub, vm->rsub);  DISPATCH();
	TARGET(OP_MUL): BINARY(NUM_VAL,  *,  vm->mul, vm->rmul);  DISPATCH();
	TARGET(OP_LT):  BINARY(BOOL_VAL, <,  vm->lt, NULL);       DISPATCH();
	TARGET(OP_LE):  BINARY(BOOL_VAL, <=, vm->le, NULL);       DISPATCH();
	TARGET(OP_GT):  BINARY(BOOL_VAL, >,  vm->gt, NULL);       DISPATCH();
	TARGET(OP_GE):  BINARY(BOOL_VAL, >=, vm->ge, NULL);       DISPATCH();
	TARGET(OP_EQ): {
		if(IS_NUM(peek2(vm)) || IS_BOOL(peek2(vm)) || IS_NULL(peek2(vm))) {
			push(vm, BOOL_VAL(valueEquals(pop(vm), pop(vm))));
		} else {
			ObjClass *cls = getClass(vm, peek2(vm));

			Value eq;
			if(hashTableGet(&cls->methods, vm->eq, &eq)) {
				SAVE_FRAME();
				if(!callValue(vm, eq, 1)) {
					LOAD_FRAME();
					UNWIND_STACK(vm);
				}
				LOAD_FRAME()
			} else {
				push(vm, BOOL_VAL(valueEquals(pop(vm), pop(vm))));
			}
		}

		DISPATCH();
	}
	TARGET(OP_NEG): {
		if(IS_NUM(peek(vm))) {
			push(vm, NUM_VAL(-AS_NUM(pop(vm))));
		} else {
			ObjClass *cls = getClass(vm, peek(vm));

			SAVE_FRAME();
			if(!invokeMethod(vm, cls, vm->neg, 0)) {
				blRaise(vm, "TypeException", "Operator unary - not "
							"defined for type %s", cls->name->data);
				UNWIND_STACK(vm);
			}
			LOAD_FRAME();
		}
		DISPATCH();
	}
	TARGET(OP_IS): {
		Value b = pop(vm);
		Value a = pop(vm);

		if(!IS_CLASS(b)) {
			blRaise(vm, "TypeException", "Right operand of `is` must be a class.");
			UNWIND_STACK(vm);
		}

		push(vm, BOOL_VAL(isInstance(vm, a, AS_CLASS(b))));
		DISPATCH();
	}
	TARGET(OP_POW): {
		if(!IS_NUM(peek(vm)) || !IS_NUM(peek2(vm))) {
			blRaise(vm, "TypeException", "Operands of `^` must be numbers");
			UNWIND_STACK(vm);
		}

		double y = AS_NUM(pop(vm));
		double x = AS_NUM(pop(vm));
		push(vm, NUM_VAL(pow(x, y)));
		DISPATCH();
	}
	TARGET(OP_NOT):
		push(vm, BOOL_VAL(!isValTrue(pop(vm))));
		DISPATCH();
	TARGET(OP_ARR_GET): {
		if(IS_LIST(peek2(vm))) {
			if(!IS_NUM(peek(vm)) || !isInt(AS_NUM(peek(vm)))) {
				blRaise(vm, "TypeException", "Index of list access must be an integer.");
				UNWIND_STACK(vm);
			}

			double index = AS_NUM(pop(vm));
			ObjList *list = AS_LIST(pop(vm));

			if(index < 0 || index >= list->count) {
				blRaise(vm, "IndexOutOfBoundException",
					"List index out of bound: %g.", index);
				UNWIND_STACK(vm);
			}

			push(vm, list->arr[(size_t)index]);
		} else if(IS_TUPLE(peek2(vm))) {
			if(!IS_NUM(peek(vm)) || !isInt(AS_NUM(peek(vm)))) {
				blRaise(vm, "TypeException", "Index of list access must be an integer.");
				UNWIND_STACK(vm);
			}

			double index = AS_NUM(pop(vm));
			ObjTuple *tuple = AS_TUPLE(pop(vm));

			if(index < 0 || index >= tuple->size) {
				blRaise(vm, "IndexOutOfBoundException",
					"Tuple index out of bound: %g.", index);
				UNWIND_STACK(vm);
			}

			push(vm, tuple->arr[(size_t)index]);
		} else if(IS_STRING(peek2(vm))) {
			if(!IS_NUM(peek(vm)) || !isInt(AS_NUM(peek(vm)))) {
				blRaise(vm, "TypeException", "Index of string access must be an integer.");
				UNWIND_STACK(vm);
			}

			double index = AS_NUM(pop(vm));
			ObjString *str = AS_STRING(pop(vm));

			if(index < 0 || index >= str->length) {
				blRaise(vm, "IndexOutOfBoundException",
				    "String index out of bound: %lu.", index);
				UNWIND_STACK(vm);
			}

			char character = str->data[(size_t)index];
			push(vm, OBJ_VAL(copyString(vm, &character, 1, true)));
		} else {
			ObjClass *cls = getClass(vm, peek2(vm));

			SAVE_FRAME();
			if(!invokeMethod(vm, cls, vm->get, 1)) {
				blRaise(vm, "TypeException", "Operator get [] "
					"not defined for type %s", cls->name->data);
				UNWIND_STACK(vm);
			}
			LOAD_FRAME();
		}
		DISPATCH();
	}
	TARGET(OP_ARR_SET): {
		if(IS_LIST(peek2(vm))) {
			if(!IS_NUM(peek(vm)) || !isInt(AS_NUM(peek(vm)))) {
				blRaise(vm, "TypeException", "Index of list access must be an integer.");
				UNWIND_STACK(vm);
			}


			double index = AS_NUM(pop(vm));
			ObjList *list = AS_LIST(pop(vm));
			Value val = pop(vm);

			if(index < 0 || index >= list->count) {
				blRaise(vm, "IndexOutOfBoundException",
					"List index out of bound: %g.", index);
				UNWIND_STACK(vm);
			}

			list->arr[(size_t)index] = val;
			push(vm, val);
		} else {
			// Invert arguments so that the object is the first argument for the call
			Value index = pop(vm), obj = pop(vm), val = pop(vm);
			push(vm, obj);
			push(vm, index);
			push(vm, val);

			ObjClass *cls = getClass(vm, peekn(vm, 2));

			SAVE_FRAME();
			if(!invokeMethod(vm, cls, vm->set, 2)) {
				blRaise(vm, "TypeException", "Operator set [] "
					"not defined for type %s", cls->name->data);
				UNWIND_STACK(vm);
			}
			LOAD_FRAME();
		}
		DISPATCH();
	}
	TARGET(OP_GET_FIELD): {
		Value v = pop(vm);
		if(!getFieldFromValue(vm, v, GET_STRING())) {
			UNWIND_STACK(vm);
		}
		DISPATCH();
	}
	TARGET(OP_SET_FIELD): {
		Value v = pop(vm);
		if(!setFieldOfValue(vm, v, GET_STRING(), peek(vm))) {
			UNWIND_STACK(vm);
		}
		DISPATCH();
	}
	TARGET(OP_JUMP): {
		int16_t off = NEXT_SHORT();
		ip += off;
		DISPATCH();
	}
	TARGET(OP_JUMPF): {
		int16_t off = NEXT_SHORT();
		if(!isValTrue(pop(vm))) ip += off;
		DISPATCH();
	}
	TARGET(OP_JUMPT): {
		int16_t off = NEXT_SHORT();
		if(isValTrue(pop(vm))) ip += off;
		DISPATCH();
	}
	TARGET(OP_NULL):
		push(vm, NULL_VAL);
		DISPATCH();
	TARGET(OP_CALL): {
		uint8_t argc = NEXT_CODE();
		goto call;
	TARGET(OP_CALL_0):
	TARGET(OP_CALL_1):
	TARGET(OP_CALL_2):
	TARGET(OP_CALL_3):
	TARGET(OP_CALL_4):
	TARGET(OP_CALL_5):
	TARGET(OP_CALL_6):
	TARGET(OP_CALL_7):
	TARGET(OP_CALL_8):
	TARGET(OP_CALL_9):
	TARGET(OP_CALL_10):
		argc = op - OP_CALL_0;
call:
		SAVE_FRAME();
		if(!callValue(vm, peekn(vm, argc), argc)) {
			LOAD_FRAME();
			UNWIND_STACK(vm);
		}
		LOAD_FRAME();

		DISPATCH();
	}
	TARGET(OP_INVOKE): {
		uint8_t argc = NEXT_CODE();
		goto invoke;
	TARGET(OP_INVOKE_0):
	TARGET(OP_INVOKE_1):
	TARGET(OP_INVOKE_2):
	TARGET(OP_INVOKE_3):
	TARGET(OP_INVOKE_4):
	TARGET(OP_INVOKE_5):
	TARGET(OP_INVOKE_6):
	TARGET(OP_INVOKE_7):
	TARGET(OP_INVOKE_8):
	TARGET(OP_INVOKE_9):
	TARGET(OP_INVOKE_10):
		argc = op - OP_INVOKE_0;
invoke:;
		ObjString *name = GET_STRING();

		SAVE_FRAME();
		if(!invokeFromValue(vm, name, argc)) {
			UNWIND_STACK(vm);
		}
		LOAD_FRAME();

		DISPATCH();
	}
	TARGET(OP_SUPER): {
		uint8_t argc = NEXT_CODE();
		goto sup_invoke;
	TARGET(OP_SUPER_0):
	TARGET(OP_SUPER_1):
	TARGET(OP_SUPER_2):
	TARGET(OP_SUPER_3):
	TARGET(OP_SUPER_4):
	TARGET(OP_SUPER_5):
	TARGET(OP_SUPER_6):
	TARGET(OP_SUPER_7):
	TARGET(OP_SUPER_8):
	TARGET(OP_SUPER_9):
	TARGET(OP_SUPER_10):
		argc = op - OP_SUPER_0;
sup_invoke:;
		ObjString *name = GET_STRING();

		SAVE_FRAME();
		ObjInstance *inst = AS_INSTANCE(peekn(vm, argc));
		if(!invokeMethod(vm, inst->base.cls->superCls, name, argc)) {
			UNWIND_STACK(vm);
		}
		LOAD_FRAME();

		DISPATCH();
	}
	TARGET(OP_RETURN): {
		Value ret = pop(vm);

		while(frame->handlerc > 0) {
			Handler *h = &frame->handlers[--frame->handlerc];
			if(h->type == HANDLER_ENSURE) {
				UNWIND_HANDLER(h, NUM_VAL((double) CAUSE_RETURN), ret);
				LOAD_FRAME();
				DISPATCH();
			}
		}

		closeUpvalues(vm, frameStack);

		vm->sp = frameStack;
		push(vm, ret);

		if(--vm->frameCount == depth) {
			return true;
		}

		LOAD_FRAME();
		vm->module = fn->c.module;

		DISPATCH();
	}
	TARGET(OP_IMPORT):
	TARGET(OP_IMPORT_AS):
	TARGET(OP_IMPORT_FROM): {
		ObjString *name = GET_STRING();
		if(!importModule(vm, name)) {
			blRaise(vm, "ImportException", "Cannot load module `%s`.", name->data);
			UNWIND_STACK(vm);
		}


		if(op == OP_IMPORT || op == OP_IMPORT_AS) {
			//define name for the module in the importing module
			hashTablePut(&vm->module->globals, op == OP_IMPORT ?
						name : GET_STRING(), OBJ_VAL(getModule(vm, name)));
		}

		//call the module's main if first time import
		if(!valueEquals(peek(vm), NULL_VAL)) {
			SAVE_FRAME();
			ObjClosure *closure = newClosure(vm, AS_FUNC(peek(vm)));
			*(vm->sp - 1) = OBJ_VAL(closure); 
			callFunction(vm, closure, 0);
			LOAD_FRAME();
		}

		DISPATCH();
	}
	TARGET(OP_IMPORT_NAME): {
		ObjModule *m = getModule(vm, GET_STRING());
		ObjString *n = GET_STRING();

		if(n->data[0] == '*') {
			hashTableImportNames(&vm->module->globals, &m->globals);
		} else {
			Value val;
			if(!hashTableGet(&m->globals, n, &val)) {
				blRaise(vm, "NameException", "Name `%s` not defined in module `%s`.", n->data, m->name->data);
				UNWIND_STACK(vm);
			} 

			hashTablePut(&vm->module->globals, n, val);
		}
		DISPATCH();
	}
	TARGET(OP_APPEND_LIST): {
		listAppend(vm, AS_LIST(peek2(vm)), peek(vm));
		pop(vm);
		DISPATCH();
	}
	TARGET(OP_NEW_LIST):
		push(vm, OBJ_VAL(newList(vm, 0)));
		DISPATCH();
	TARGET(OP_NEW_TUPLE): {
		uint8_t size = NEXT_CODE();
		ObjTuple *t = newTuple(vm, size);

		for(int i = size - 1; i >= 0; i--) t->arr[i] = pop(vm);

		push(vm, OBJ_VAL(t));
		DISPATCH();
	}
	TARGET(OP_NEW_CLOSURE): {
		ObjClosure *closure = newClosure(vm, AS_FUNC(GET_CONST()));
		push(vm, OBJ_VAL(closure));

		for(uint8_t i = 0; i < closure->fn->upvaluec; i++) {
			uint8_t isLocal = NEXT_CODE();
			uint8_t index = NEXT_CODE();
			if(isLocal) {
				closure->upvalues[i] = captureUpvalue(vm, frame->stack + index);
			} else {
				closure->upvalues[i] = frame->fn.closure->upvalues[i];
			}
		}
		DISPATCH();
	}
	TARGET(OP_NEW_CLASS):
		createClass(vm, GET_STRING(), vm->objClass);
		DISPATCH();
	TARGET(OP_NEW_SUBCLASS):
		if(!IS_CLASS(peek(vm))) {
			blRaise(vm, "TypeException", "Superclass in class declaration must be a Class.");
			UNWIND_STACK(vm);
		}
		createClass(vm, GET_STRING(), AS_CLASS(pop(vm)));
		DISPATCH();
	TARGET(OP_UNPACK):
		if(!IS_LIST(peek(vm)) && !IS_TUPLE(peek(vm))) {
			blRaise(vm, "TypeException", "Can unpack only Tuple "
				"or List, got %s.", getClass(vm, peek(vm))->name->data);
			UNWIND_STACK(vm);
		}

		uint8_t num = NEXT_CODE();
		Obj *seq = AS_OBJ(pop(vm));
		
		Value *arr = NULL;
		size_t size = 0;

		switch(seq->type) {
		case OBJ_TUPLE:
			arr = ((ObjTuple*)seq)->arr;
			size = ((ObjTuple*)seq)->size;
			break;
		case OBJ_LIST:
			arr = ((ObjList*)seq)->arr;
			size = ((ObjList*)seq)->count;
			break;
		default: 
			UNREACHABLE();
			break;
		}

		if(num > size) {
			blRaise(vm, "TypeException", "Too little values to unpack.");
			UNWIND_STACK(vm);
		}

		for(int i = 0; i < num; i++) push(vm, arr[i]);

		DISPATCH();
	TARGET(OP_DEF_METHOD): {
		ObjClass *cls = AS_CLASS(peek2(vm));
		ObjString *methodName = GET_STRING();
		hashTablePut(&cls->methods, methodName, pop(vm));
		DISPATCH();
	}
	TARGET(OP_NAT_METHOD): {
		ObjClass *cls = AS_CLASS(peek(vm));
		ObjString *methodName = GET_STRING();
		ObjNative *native = AS_NATIVE(GET_CONST());

		native->fn = resolveBuiltIn(vm->module->name->data, cls->name->data, methodName->data);
		if(native->fn == NULL) {
			blRaise(vm, "Exception", "Cannot resolve native method %s().", native->c.name->data);
			UNWIND_STACK(vm);
		}

		hashTablePut(&cls->methods, methodName, OBJ_VAL(native));
		DISPATCH();
	}
	TARGET(OP_DEFINE_NATIVE): {
		ObjString *name = GET_STRING();
		ObjNative *nat  = AS_NATIVE(pop(vm));

		nat->fn = resolveBuiltIn(vm->module->name->data, NULL, name->data);
		if(nat->fn == NULL) {
			blRaise(vm, "Exception", "Cannot resolve native %s.", nat->c.name->data);
			UNWIND_STACK(vm);
		}

		hashTablePut(&vm->module->globals, name, OBJ_VAL(nat));
		DISPATCH();
	}
	TARGET(OP_GET_CONST):
		push(vm, GET_CONST());
		DISPATCH();
	TARGET(OP_DEFINE_GLOBAL):
		hashTablePut(&vm->module->globals, GET_STRING(), pop(vm));
		DISPATCH();
	TARGET(OP_GET_GLOBAL): {
		ObjString *name = GET_STRING();
		if(!hashTableGet(&vm->module->globals, name, vm->sp)) {
			if(!hashTableGet(&vm->core->globals, name, vm->sp)) {
				blRaise(vm, "NameException", "Name `%s` is not defined.", name->data);
				UNWIND_STACK(vm);
			}
		}
		vm->sp++;
		DISPATCH();
	}
	TARGET(OP_SET_GLOBAL): {
		ObjString *name = GET_STRING();
		if(hashTablePut(&vm->module->globals, name, peek(vm))) {
			blRaise(vm, "NameException", "Name `%s` is not defined.", name->data);
			UNWIND_STACK(vm);
		}
		DISPATCH();
	}
	TARGET(OP_SETUP_EXCEPT): 
	TARGET(OP_SETUP_ENSURE): {
		//setup the handler address and save stackpointer
		uint16_t handlerOff = NEXT_SHORT();
		Handler *handler = &frame->handlers[frame->handlerc++];
		handler->type = op == OP_SETUP_EXCEPT ? HANDLER_EXCEPT : HANDLER_ENSURE;
		handler->handler = ip + handlerOff;
		handler->savesp = vm->sp;
		DISPATCH();
	}
	TARGET(OP_ENSURE_END): {
		UnwindCause cause = AS_NUM(peek2(vm));

		switch(cause) {
		case CAUSE_EXCEPT:
			// if we still have the exception on top of the stack
			if(!IS_NULL(peek(vm))) {
				// continue unwinding
				vm->exception = AS_INSTANCE(peek(vm));
				UNWIND_STACK(vm);
			}
			break;
		case CAUSE_RETURN:
			while(frame->handlerc > 0) {
				Handler *h = &frame->handlers[--frame->handlerc];
				if(h->type == HANDLER_ENSURE) {
					Value ret = pop(vm), cause = pop(vm);
					UNWIND_HANDLER(h, cause, ret);
					LOAD_FRAME();
					DISPATCH();
				}
			}
			
			GOTO(OP_RETURN);
			break;
		default: break;
		}

		DISPATCH();
	}
	TARGET(OP_POP_HANDLER): {
		frame->handlerc--;
		DISPATCH();
	}
	TARGET(OP_RAISE): {
		Value exc = peek(vm);

		if(!IS_INSTANCE(exc)) {
			blRaise(vm, "TypeException", "Can only raise Object instances.");
			UNWIND_STACK(vm);
		}

		ObjStackTrace *st = newStackTrace(vm);
		push(vm, OBJ_VAL(st));

		ObjInstance *excInst = AS_INSTANCE(exc);
		hashTablePut(&excInst->fields, vm->stField, OBJ_VAL(st));

		pop(vm);
		pop(vm);

		vm->exception = AS_INSTANCE(exc);
		
		UNWIND_STACK(vm);
	}
	TARGET(OP_GET_LOCAL):
		push(vm, frameStack[NEXT_CODE()]);
		DISPATCH();
	TARGET(OP_SET_LOCAL):
		frameStack[NEXT_CODE()] = peek(vm);
		DISPATCH();
	TARGET(OP_GET_UPVALUE):
		push(vm, *closure->upvalues[NEXT_CODE()]->addr);
		DISPATCH();
	TARGET(OP_SET_UPVALUE):
		*closure->upvalues[NEXT_CODE()]->addr = peek(vm);
		DISPATCH();
	TARGET(OP_POP):
		pop(vm);
		DISPATCH();
	TARGET(OP_CLOSE_UPVALUE):
		closeUpvalues(vm, vm->sp - 1);
		pop(vm);
		DISPATCH();
	TARGET(OP_DUP):
		*vm->sp = *(vm->sp - 1);
		vm->sp++;
		DISPATCH();

	TARGET(OP_SIGN_CONT):
	TARGET(OP_SIGN_BRK):
		UNREACHABLE();
		return false;
	}

	UNREACHABLE();
	return false;
}

static void printStackTrace(BlangVM *vm, ObjStackTrace *st) {
	fprintf(stderr, "Traceback (most recent call last):\n");

	// Print stacktrace in reverse order of recording (most recent call last)
	char *stacktrace = st->trace;
	int lastnl = st->length;
	for(int i = lastnl - 1; i > 0; i--) {
		if(stacktrace[i - 1] == '\n') {
			fprintf(stderr, "    %.*s", lastnl - i, stacktrace + i);
			lastnl = i;
		}
	}
	fprintf(stderr, "    %.*s", lastnl, stacktrace);

	// print the exception instance information
	Value v;
	ObjInstance *exc = (ObjInstance*) vm->exception;
	bool found = hashTableGet(&exc->fields, copyString(vm, "err", 3, true), &v);

	if(found && IS_STRING(v)) {
		fprintf(stderr, "%s: %s\n", exc->base.cls->name->data, AS_STRING(v)->data);
	} else {
		fprintf(stderr, "%s\n", exc->base.cls->name->data);
	}
}

static bool unwindStack(BlangVM *vm, int depth) {
	Value stVal;
	hashTableGet(&vm->exception->fields, vm->stField, &stVal);
	ObjStackTrace *st = AS_STACK_TRACE(stVal);

	for(;vm->frameCount > depth; vm->frameCount--) {
		Frame *frame = &vm->frames[vm->frameCount - 1];

		stRecordFrame(vm, st, frame, vm->frameCount);

		// if current frame has except or ensure handlers
		if(frame->handlerc > 0) {
			Handler *h = &frame->handlers[--frame->handlerc];
			UNWIND_HANDLER(h, NUM_VAL((double) CAUSE_EXCEPT), OBJ_VAL(vm->exception));
			vm->exception = NULL;
			return true;
		}

	}

	// We have reached the bottom of the stack, print the stacktrace and exit
	if(vm->frameCount == 0) {
		printStackTrace(vm, st);
		reset(vm);
	}

	return false;
}

// API

EvalResult blEvaluate(BlangVM *vm, const char *fpath, const char *src) {
	return blEvaluateModule(vm, fpath, "__main__", src);
}

EvalResult blEvaluateModule(BlangVM *vm, const char *fpath, const char *module, const char *src) {
	Parser p;

	Stmt *program = parse(&p, fpath, src);
	if(p.hadError) {
		freeStmt(program);
		return VM_SYNTAX_ERR;
	}

	ObjString *name = copyString(vm, module, strlen(module), true);
	ObjFunction *fn = compileWithModule(vm, name, program);

	freeStmt(program);
	if(fn == NULL) {
		return VM_COMPILE_ERR;
	}

	push(vm, OBJ_VAL(fn));

	ObjClosure *closure = newClosure(vm, fn);

	pop(vm);

	push(vm, OBJ_VAL(closure));
	
	EvalResult res = blCall(vm, 0);

	if(res == VM_EVAL_SUCCSESS) pop(vm);

	return res;
}

static EvalResult blFinishCall(BlangVM *vm, int depth) {
	if(vm->frameCount > depth) {
		if(!runEval(vm, depth)) return VM_RUNTIME_ERR;
	}

	// reset API stack if we are on a native frame
	if(vm->frameCount != 0 && vm->frames[vm->frameCount - 1].fn.type == OBJ_NATIVE) {
		vm->apiStack = vm->frames[vm->frameCount - 1].stack;
	} else {
		vm->apiStack = vm->stack;
	}

	return VM_EVAL_SUCCSESS;
}

static void blErrorCall(BlangVM *vm, int depth) {
	if(depth == 0) {
		if(vm->frameCount > 0) unwindStack(vm, 0);
		reset(vm);
	}
}

EvalResult blCall(BlangVM *vm, uint8_t argc) {
	int depth = vm->frameCount;

	if(!callValue(vm, peekn(vm, argc), argc)) {
		blErrorCall(vm, depth);
		return VM_RUNTIME_ERR;
	}

	return blFinishCall(vm, depth);
}

EvalResult blCallMethod(BlangVM *vm, const char *name, uint8_t argc) {
	int depth = vm->frameCount;
	ObjString *meth = copyString(vm, name, strlen(name), false);

	if(!invokeFromValue(vm, meth, argc)) {
		blErrorCall(vm, depth);
		return VM_RUNTIME_ERR;
	}

	return blFinishCall(vm, depth);
}

void blRaise(BlangVM *vm, const char* cls, const char *err, ...) {
	if(!blGetGlobal(vm, NULL, cls)) return;
	assert(IS_CLASS(peek(vm)), "Trying to instatiate a non class object");

	ObjInstance *excInst = newInstance(vm, AS_CLASS(pop(vm)));
	push(vm, OBJ_VAL(excInst));

	ObjStackTrace *st = newStackTrace(vm);
	hashTablePut(&excInst->fields, vm->stField, OBJ_VAL(st));

	if(err != NULL) {
		char errStr[1024] = {0};
		va_list args;
		va_start(args, err);
		vsnprintf(errStr, sizeof(errStr) - 1, err, args);
		va_end(args);
		
		blPushString(vm, errStr);
		blSetField(vm, -2, "err");
		blPop(vm);
	}

	pop(vm);
	vm->exception = excInst;
}

void blSetField(BlangVM *vm, int slot, const char *name) {
	Value val = apiStackSlot(vm, slot);
	setFieldOfValue(vm, val, copyString(vm, name, strlen(name), false), peek(vm));
}

bool blGetField(BlangVM *vm, int slot, const char *name) {
    Value val = apiStackSlot(vm, slot);
	return getFieldFromValue(vm, val, copyString(vm, name, strlen(name), false));
}

void blInitCommandLineArgs(int argc, const char **argv) {
	sysInitArgs(argc, argv);
}

void blAddImportPath(BlangVM *vm, const char *path) {
	listAppend(vm, vm->importpaths, OBJ_VAL(copyString(vm, path, strlen(path), false)));
}
