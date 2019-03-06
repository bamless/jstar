#include "vm.h"
#include "opcode.h"
#include "import.h"
#include "modules.h"
#include "core.h"
#include "util.h"
#include "sys.h"
#include "blang.h"
#include "options.h"

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

static bool unwindStack(BlangVM *vm);

static void reset(BlangVM *vm) {
	vm->sp = vm->stack;
	vm->frameCount = 0;
	vm->exception = NULL;
}

BlangVM *blNewVM() {
	BlangVM *vm = malloc(sizeof(*vm));

	vm->importpaths = NULL;
	vm->currCompiler = NULL;
	vm->ctor = NULL;

	vm->exception = NULL;
	sbuf_create(&vm->stacktrace);
	vm->lastTracedFrame = -1;

	vm->clsClass  = NULL;
	vm->objClass  = NULL;
	vm->strClass  = NULL;
	vm->boolClass = NULL;
	vm->lstClass  = NULL;
	vm->numClass  = NULL;
	vm->funClass  = NULL;
	vm->modClass  = NULL;
	vm->nullClass = NULL;
	vm->excClass  = NULL;

	reset(vm);

	initHashTable(&vm->modules);
	initHashTable(&vm->strings);

	vm->module = NULL;

	// init GC
	vm->nextGC = INIT_GC;
	vm->objects = NULL;
	vm->disableGC = false;

	vm->allocated = 0;
	vm->reachedStack = NULL;
	vm->reachedCapacity = 0;
	vm->reachedCount = 0;

	// Create constants strings
	vm->ctor = copyString(vm, CTOR_STR, strlen(CTOR_STR));

	vm->add = copyString(vm, "__add__", 7);
	vm->sub = copyString(vm, "__sub__", 7);
	vm->mul = copyString(vm, "__mul__", 7);
	vm->div = copyString(vm, "__div__", 7);
	vm->mod = copyString(vm, "__mod__", 7);
	vm->get = copyString(vm, "__get__", 7);
	vm->set = copyString(vm, "__set__", 7);

	vm->radd = copyString(vm, "__radd__", 8);
	vm->rsub = copyString(vm, "__rsub__", 8);
	vm->rmul = copyString(vm, "__rmul__", 8);
	vm->rdiv = copyString(vm, "__rdiv__", 8);
	vm->rmod = copyString(vm, "__rmod__", 8);

	vm->lt  = copyString(vm, "__lt__", 6);
	vm->le  = copyString(vm, "__le__", 6);
	vm->gt  = copyString(vm, "__gt__", 6);
	vm->ge  = copyString(vm, "__ge__", 6);
	vm->eq  = copyString(vm, "__eq__", 6);

	vm->neg = copyString(vm, "__neg__", 7);

	// Bootstrap the core module
	initCoreLibrary(vm);

	// This is called after initCoreLibrary in order to correctly assign the
	// List class to the object since classes are created during initialization
	vm->importpaths = newList(vm, 8);

	return vm;
}

void blFreeVM(BlangVM *vm) {
	reset(vm);

	sbuf_destroy(&vm->stacktrace);

	freeHashTable(&vm->strings);
	freeHashTable(&vm->modules);
	freeObjects(vm);

#ifdef DBG_PRINT_GC
	printf("Allocated at exit: %lu bytes.\n", vm->allocated);
#endif

	free(vm);
}

void push(BlangVM *vm, Value v) {
	*vm->sp++ = v;
}

Value pop(BlangVM *vm) {
	return *--vm->sp;
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

static bool callFunction(BlangVM *vm, ObjFunction *func, uint8_t argc) {
	if(func->defaultc != 0) {
		uint8_t most  = func->argsCount;
		uint8_t least = most - func->defaultc;

		if(argc > most || argc < least) {
			blRaise(vm, "TypeException", "Function `%s` takes at %s %d args, "
						 "%d supplied.", func->name->data, argc > most ?
						 "most" : "least", argc > most ? most : least, argc);
			return false;
		}

		// push remaining args taking the default value
		for(uint8_t i = argc - least; i < func->defaultc; i++) {
			push(vm, func->defaults[i]);
		}
	} else if(func->argsCount != argc) {
		blRaise(vm, "TypeException", "Function `%s` takes exactly %d args, "
		        "%d supplied.", func->name->data, func->argsCount, argc);
		return false;
	}

	if(vm->frameCount == FRAME_SZ) {
		blRaise(vm, "StackOverflowException", NULL);
		return false;
	}

	Frame *callFrame = &vm->frames[vm->frameCount++];
	callFrame->fn = func;
	callFrame->ip = func->chunk.code;
	callFrame->stack = vm->sp - (func->argsCount + 1);
	callFrame->handlerc = 0;

	vm->module = func->module;

	return true;
}

static bool callNative(BlangVM *vm, ObjNative *native, uint8_t argc) {
	if(native->defaultc != 0) {
		uint8_t most  = native->argsCount;
		uint8_t least = most - native->defaultc;

		if(argc > most || argc < least) {
			blRaise(vm, "TypeException", "Native `%s` takes at %s %d args, "
						 "%d supplied.", native->name->data, argc > most ?
						 "most" : "least", argc > most ? most : least, argc);
			return false;
		}

		// push remaining args taking the default value
		for(uint8_t i = argc - least; i < native->defaultc; i++) {
			push(vm, native->defaults[i]);
		}
	} else if(native->argsCount != argc) {
		blRaise(vm, "TypeException", "Native `%s` takes exactly %d args, "
				"%d supplied.", native->name->data, native->argsCount, argc);
		return false;
	}

	Value ret;
	if(!native->fn(vm, vm->sp - (native->argsCount + 1), &ret)) {
		blRaise(vm, "Exception", "Failed to call native %s().", native->name->data);
		return false;
	}
	vm->sp -= native->argsCount + 1;
	push(vm, ret);

	return vm->exception == NULL;
}

static bool callValue(BlangVM *vm, Value callee, uint8_t argc) {
	if(IS_OBJ(callee)) {
		switch(OBJ_TYPE(callee)) {
		case OBJ_FUNCTION:
			return callFunction(vm, AS_FUNC(callee), argc);
		case OBJ_NATIVE:
			return callNative(vm, AS_NATIVE(callee), argc);
		case OBJ_BOUND_METHOD: {
			ObjBoundMethod *m = AS_BOUND_METHOD(callee);
			vm->sp[-argc - 1] = m->bound;
			return m->method->type == OBJ_FUNCTION ?
			        callFunction(vm, (ObjFunction*)m->method, argc) :
			        callNative(vm, (ObjNative*)m->method, argc);
		}
		case OBJ_CLASS: {
			ObjClass *cls = AS_CLASS(callee);
			vm->sp[-argc - 1] = OBJ_VAL(newInstance(vm, cls));

			Value ctor;
			if(hashTableGet(&cls->methods, vm->ctor, &ctor)) {
				return callFunction(vm, AS_FUNC(ctor), argc);
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

static void createClass(BlangVM *vm, ObjString *name, ObjClass *superCls) {
	ObjClass *cls = newClass(vm, name, superCls);

	if(superCls != NULL) {
		hashTableMerge(&cls->methods, &superCls->methods);
	}

	push(vm, OBJ_VAL(cls));
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
				if(!IS_OBJ(f) && (IS_FUNC(f) || IS_NATIVE(f)
								|| IS_CLASS(f) || IS_BOUND_METHOD(f))) {
					return callValue(vm, f, argc);
				}
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
	return ((IS_BOOL(val) && AS_BOOL(val))
	      ||(IS_NUM(val) && AS_NUM(val) != 0)
	      ||(IS_STRING(val) && AS_STRING(val)->length != 0)
	      ||(IS_OBJ(val) && !IS_STRING(val)));
}

static ObjString* stringConcatenate(BlangVM *vm, ObjString *s1, ObjString *s2) {
	size_t length = s1->length + s2->length;
	char *data = GC_ALLOC(vm, length + 1);
	memcpy(data, s1->data, s1->length);
	memcpy(data + s1->length, s2->data, s2->length);
	data[length] = '\0';
	return newStringFromBuf(vm, data, length);
}

static bool callBinaryOverload(BlangVM *vm, ObjString *name, ObjString *reverse) {
	ObjClass *cls = getClass(vm, peek2(vm));

	if(!invokeMethod(vm, cls, name, 1)) {
		// swap callee and arg
		Value b = peek(vm);
		vm->sp[-1] = vm->sp[-2];
		vm->sp[-2] = b;

		// reset exception
		vm->exception = NULL;

		cls = getClass(vm, peek2(vm));
		if(!invokeMethod(vm, cls, reverse == NULL ? name : reverse, 1)) {
			return false;
		}
	}

	return true;
}

static bool runEval(BlangVM *vm) {
	register Frame *frame;
	register Value *frameStack;
	register ObjFunction *fn;
	register uint8_t *ip;

	#define LOAD_FRAME() \
		frame = &vm->frames[vm->frameCount - 1]; \
		frameStack = frame->stack; \
		fn = frame->fn; \
		ip = frame->ip; \

	#define SAVE_FRAME() frame->ip = ip;

	#define NEXT_CODE()  (*ip++)
	#define NEXT_SHORT() (ip += 2, ((uint16_t) ip[-2] << 8) | ip[-1])

	#define GET_CONST()  (fn->chunk.consts.arr[NEXT_CODE()])
	#define GET_STRING() (AS_STRING(GET_CONST()))

	#define BINARY(type, op, overload, reverse) do { \
		if(IS_NUM(peek(vm)) && IS_NUM(peek2(vm))) { \
			double b = AS_NUM(pop(vm)); \
			double a = AS_NUM(pop(vm)); \
			push(vm, type(a op b)); \
		} else { \
			SAVE_FRAME(); \
			if(!callBinaryOverload(vm, overload, reverse)) {   \
				ObjString *t1 = getClass(vm, peek(vm))->name;  \
				ObjString *t2 = getClass(vm, peek2(vm))->name; \
				blRaise(vm, "TypeException", "Operator %s not defined "  \
				            "for types %s, %s", #op, t1->data, t2->data); \
				UNWIND_STACK(vm); \
			} \
			LOAD_FRAME(); \
		} \
	} while(0)

	#define UNWIND_STACK(vm) do { \
		SAVE_FRAME() \
		if(!unwindStack(vm)) { \
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

	#else

		#define TARGET(op) case op
		#define DISPATCH() goto decode
		#define DECODE(op) \
		decode: \
			PRINT_DBG_STACK(); \
			switch((op = NEXT_CODE()))

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
	TARGET(OP_SUB): BINARY(NUM_VAL,  -,   vm->sub, vm->rsub); DISPATCH();
	TARGET(OP_MUL): BINARY(NUM_VAL,  *,   vm->mul, vm->rmul); DISPATCH();
	TARGET(OP_LT):  BINARY(BOOL_VAL, <,  vm->lt, NULL);       DISPATCH();
	TARGET(OP_LE):  BINARY(BOOL_VAL, <=, vm->le, NULL);       DISPATCH();
	TARGET(OP_GT):  BINARY(BOOL_VAL, >,  vm->gt, NULL);       DISPATCH();
	TARGET(OP_GE):  BINARY(BOOL_VAL, >=, vm->ge, NULL);       DISPATCH();
	TARGET(OP_EQ): {
		ObjClass *cls = getClass(vm, peek2(vm));

		SAVE_FRAME();
		if(!invokeMethod(vm, cls, vm->eq, 1)) {
			vm->exception = NULL;

			push(vm, BOOL_VAL(valueEquals(pop(vm), pop(vm))));
			DISPATCH();
		}
		LOAD_FRAME()

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
	TARGET(OP_NOT):
		push(vm, BOOL_VAL(!isValTrue(pop(vm))));
		DISPATCH();
	TARGET(OP_ARR_GET): {
		if(IS_LIST(peek2(vm))) {
			if(!IS_NUM(peek(vm)) || !isInt(AS_NUM(peek(vm)))) {
				blRaise(vm, "TypeException", "Index of list access must be an integer.");
				UNWIND_STACK(vm);
			}

			size_t index = AS_NUM(pop(vm));
			ObjList *list = AS_LIST(pop(vm));

			if(index >= list->count) {
				blRaise(vm, "IndexOutOfBoundException",
					"List index out of bound: %lu.", index);
				UNWIND_STACK(vm);
			}

			push(vm, list->arr[index]);
		} else if(IS_STRING(peek2(vm))) {
			if(!IS_NUM(peek(vm)) || !isInt(AS_NUM(peek(vm)))) {
				blRaise(vm, "TypeException", "Index of string access must be an integer.");
				UNWIND_STACK(vm);
			}

			size_t index = AS_NUM(pop(vm));
			ObjString *str = AS_STRING(pop(vm));

			if(index >= str->length) {
				blRaise(vm, "IndexOutOfBoundException",
				    "String index out of bound: %lu.", index);
				UNWIND_STACK(vm);
			}

			char character = str->data[index];
			push(vm, OBJ_VAL(copyString(vm, &character, 1)));
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
		if(IS_LIST(peekn(vm, 2))) {
			if(!IS_NUM(peek2(vm)) || !isInt(AS_NUM(peek2(vm)))) {
				blRaise(vm, "TypeException", "Index of list access must be an integer.");
				UNWIND_STACK(vm);
			}

			Value val = pop(vm);

			size_t index = AS_NUM(pop(vm));
			ObjList *list = AS_LIST(pop(vm));

			if(index >= list->count) {
				blRaise(vm, "IndexOutOfBoundException",
					"List index out of bound: %lu.", index);
				UNWIND_STACK(vm);
			}

			list->arr[index] = val;
			push(vm, val);
		} else {
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

		if(--vm->frameCount == 0) {
			return true;
		}

		vm->sp = frameStack;
		push(vm, ret);

		LOAD_FRAME();
		vm->module = fn->module;

		DISPATCH();
	}
	TARGET(OP_IMPORT_AS):
	TARGET(OP_IMPORT): {
		ObjString *name = GET_STRING();
		if(!importModule(vm, name)) {
			blRaise(vm, "ImportException", "Cannot load module `%s`.", name->data);
			UNWIND_STACK(vm);
		}

		//define name for the module in the importing module
		hashTablePut(&vm->module->globals, op == OP_IMPORT ?
					name : GET_STRING(), OBJ_VAL(getModule(vm, name)));

		//call the module's main if first time import
		if(!valueEquals(peek(vm), NULL_VAL)) {
			SAVE_FRAME();
			callValue(vm, peek(vm), 0);
			LOAD_FRAME();
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
	TARGET(OP_DEF_METHOD): {
		ObjClass *cls = AS_CLASS(peek(vm));
		ObjString *methodName = GET_STRING();
		hashTablePut(&cls->methods, methodName, GET_CONST());
		DISPATCH();
	}
	TARGET(OP_NAT_METHOD): {
		ObjClass *cls = AS_CLASS(peek(vm));
		ObjString *methodName = GET_STRING();
		ObjNative *native = AS_NATIVE(GET_CONST());

		native->fn = resolveBuiltIn(vm->module->name->data, cls->name->data, methodName->data);
		if(native->fn == NULL) {
			blRaise(vm, "Exception", "Cannot resolve native method %s().", native->name->data);
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
			blRaise(vm, "Exception", "Cannot resolve native %s.", nat->name->data);
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
		if(!hashTableGet(&vm->module->globals, name, vm->sp++)) {
			blRaise(vm, "NameException", "Name `%s` is not defined.", name->data);
			UNWIND_STACK(vm);
		}
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
	TARGET(OP_SETUP_TRY): {
		//setup the handler address and save stackpointer
		uint16_t handlerOff = NEXT_SHORT();
		Handler *handler = &frame->handlers[frame->handlerc++];
		handler->handler = ip + handlerOff;
		handler->savesp = vm->sp;
		DISPATCH();
	}
	TARGET(OP_EXC_HANDLED): {
		//exception thrown in try is caught and handled
		vm->exception = NULL;
		DISPATCH();
	}
	TARGET(OP_EXC_HANDLER_END): {
		//exception was thrown in try but no handler handled the exception
		if(vm->exception != NULL) {
			//continue to unwind stack
			UNWIND_STACK(vm);
		}
		DISPATCH();
	}
	TARGET(OP_END_TRY): {
		//no exception was thrown inside try, decrement handler count and continue execution
		frame->handlerc--;
		DISPATCH();
	}
	TARGET(OP_RAISE): {
		sbuf_clear(&vm->stacktrace);
		vm->lastTracedFrame = -1;

		Value exc = pop(vm);

		if(!IS_INSTANCE(exc)) {
			blRaise(vm, "TypeException", "Can only raise object instances.");
			UNWIND_STACK(vm);
		}

		vm->exception = AS_OBJ(exc);
		UNWIND_STACK(vm);
	}
	TARGET(OP_GET_LOCAL):
		push(vm, frameStack[NEXT_CODE()]);
		DISPATCH();
	TARGET(OP_SET_LOCAL):
		frameStack[NEXT_CODE()] = peek(vm);
		DISPATCH();
	TARGET(OP_POP):
		pop(vm);
		DISPATCH();
	TARGET(OP_DUP):
		*vm->sp = *(vm->sp - 1);
		vm->sp++;
		DISPATCH();

	TARGET(OP_SIGN_CONT):
	TARGET(OP_SING_BRK):
		UNREACHABLE();
		return false;
	}

	UNREACHABLE();
	return false;
}

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

	ObjString *name = copyString(vm, module, strlen(module));
	ObjFunction *fn = compileWithModule(vm, name, program);

	freeStmt(program);
	if(fn == NULL) {
		return VM_COMPILE_ERR;
	}

	callFunction(vm, fn, 0);

	if(!runEval(vm)) {
		return VM_RUNTIME_ERR;
	}

	return VM_EVAL_SUCCSESS;
}

static void printStackTrace(BlangVM *vm) {
	fprintf(stderr, "Traceback (most recent call last):\n");

	// Print stacktrace in reverse order of recording (most recent call last)
	char *stacktrace = sbuf_get_backing_buf(&vm->stacktrace);
	int lastnl = sbuf_get_len(&vm->stacktrace);
	for(int i = lastnl - 1; i > 0; i--) {
		if(stacktrace[i - 1] == '\n') {
			fprintf(stderr, "    %.*s", lastnl - i, stacktrace + i);
			lastnl = i;
		}
	}
	fprintf(stderr, "    %.*s", lastnl, stacktrace);

	// print the exception instance information
	Value v;
	ObjInstance *exc = (ObjInstance*)vm->exception;
	bool found = hashTableGet(&exc->fields, copyString(vm, "err", 3), &v);

	if(found && IS_STRING(v)) {
		fprintf(stderr, "%s: %s\n", exc->base.cls->name->data, AS_STRING(v)->data);
	} else {
		fprintf(stderr, "%s\n", exc->base.cls->name->data);
	}
}

static bool unwindStack(BlangVM *vm) {
	for(;vm->frameCount > 0; vm->frameCount--) {
		Frame *f = &vm->frames[vm->frameCount - 1];

		//save current frame info to stacktrace if it hasn't been saved yet
		if(vm->lastTracedFrame != vm->frameCount) {
			ObjFunction *fn = f->fn;
			size_t op = f->ip - fn->chunk.code - 1;

			char line[MAX_STRLEN_FOR_INT_TYPE(int) + 1] = { 0 };
			sprintf(line, "%d", getBytecodeSrcLine(&fn->chunk, op));
			sbuf_appendstr(&vm->stacktrace, "[line ");
			sbuf_appendstr(&vm->stacktrace, line);
			sbuf_appendstr(&vm->stacktrace, "] ");

			sbuf_appendstr(&vm->stacktrace, "module ");
			sbuf_appendstr(&vm->stacktrace, fn->module->name->data);
			sbuf_appendstr(&vm->stacktrace, " in ");

			if(fn->name != NULL) {
				sbuf_appendstr(&vm->stacktrace, fn->name->data);
				sbuf_appendstr(&vm->stacktrace, "()\n");
			} else {
				sbuf_appendstr(&vm->stacktrace, "<main>\n");
			}
		}
		//signal that current frame has been saved
		vm->lastTracedFrame = vm->frameCount;

		// if current frame has except handlers
		if(f->handlerc > 0) {
			Handler *h = &f->handlers[--f->handlerc];

			// restore vm state and set ip to handler start
			f->ip = h->handler;
			vm->sp = h->savesp;
			vm->module = f->fn->module;

			// push the exception for use in the handler and return to execution
			push(vm, OBJ_VAL(vm->exception));
			return true;
		}

	}

	// We have reached the bottom of the stack, print the stacktrace and exit
	printStackTrace(vm);
	reset(vm);
	return false;
}

void blInitCommandLineArgs(int argc, const char **argv) {
	sysInitArgs(argc, argv);
}

void blAddImportPath(BlangVM *vm, const char *path) {
	listAppend(vm, vm->importpaths, OBJ_VAL(copyString(vm, path, strlen(path))));
}
