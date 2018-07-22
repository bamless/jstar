#include "vm.h"
#include "opcode.h"
#include "import.h"
#include "modules.h"
#include "core.h"
#include "sys.h" // for intializing command line args

#include "debug/disassemble.h"

#include "parse/parser.h"
#include "parse/ast.h"

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <float.h>
#include <math.h>

static void runtimeError(VM *vm, const char* format, ...);

static void reset(VM *vm) {
	vm->sp = vm->stack;
	vm->frameCount = 0;
	vm->error = false;
}

void initVM(VM *vm) {
	vm->currCompiler = NULL;
	vm->ctor = NULL;

	vm->stack  = malloc(sizeof(Value) * STACK_SZ);
	vm->frames = malloc(sizeof(Frame) * FRAME_SZ);
	vm->stackend = vm->stack + STACK_SZ;

	vm->clsClass  = NULL;
	vm->objClass  = NULL;
	vm->strClass  = NULL;
	vm->boolClass = NULL;
	vm->lstClass  = NULL;
	vm->numClass  = NULL;
	vm->funClass  = NULL;
	vm->modClass  = NULL;
	vm->nullClass = NULL;

	reset(vm);

	initHashTable(&vm->modules);
	initHashTable(&vm->strings);

	vm->module = NULL;

	vm->nextGC = INIT_GC;
	vm->objects = NULL;
	vm->disableGC = false;

	vm->allocated = 0;
	vm->reachedStack = NULL;
	vm->reachedCapacity = 0;
	vm->reachedCount = 0;

	vm->ctor = copyString(vm, CTOR_STR, strlen(CTOR_STR));

	initCoreLibrary(vm);
}

void push(VM *vm, Value v) {
	*vm->sp++ = v;
}

Value pop(VM *vm) {
	return *--vm->sp;
}

static ObjClass *getClass(VM *vm, Value v) {
	if(IS_OBJ(v)) {
		return AS_OBJ(v)->cls;
	} else if(IS_NUM(v)) {
		return vm->numClass;
	} else if(IS_BOOL(v)) {
		return vm->boolClass;
	} else {
		return vm->nullClass;
	}
}

static bool callFunction(VM *vm, ObjFunction *func, uint8_t argc) {
	if(func->argsCount != argc) {
		runtimeError(vm, "Function `%s` expected %d args, but instead %d "
		             "supplied.", func->name->data, func->argsCount, argc);
		return false;
	}

	if(vm->frameCount == FRAME_SZ) {
		runtimeError(vm, "Stack Overflow.");
		return false;
	}

	Frame *callFrame = &vm->frames[vm->frameCount++];
	callFrame->fn = func;
	callFrame->ip = func->chunk.code;
	callFrame->stack = vm->sp - (argc + 1);

	vm->module = func->module;

	return true;
}

static bool callNative(VM *vm, ObjNative *native, uint8_t argc) {
	if(native->argsCount != argc) {
		runtimeError(vm, "Native function `%s` expexted %d args, but instead %d"
					 " supplied.", native->name->data, native->argsCount, argc);
		return false;
	}

	Value ret = native->fn(vm, vm->sp - (argc + 1));
	vm->sp -= argc + 1;
	push(vm, ret);

	return true;
}

static bool callValue(VM *vm, Value callee, uint8_t argc) {
	if(IS_OBJ(callee)) {
		switch(OBJ_TYPE(callee)) {
		case OBJ_FUNCTION:
			return callFunction(vm, AS_FUNC(callee), argc);
		case OBJ_NATIVE:
			return callNative(vm, AS_NATIVE(callee), argc);
		case OBJ_BOUND_METHOD: {
			ObjBoundMethod *m = AS_BOUND_METHOD(callee);
			vm->sp[-argc - 1] = OBJ_VAL(m->bound);
			return callFunction(vm, m->method, argc);
		}
		case OBJ_CLASS: {
			ObjClass *cls = AS_CLASS(callee);
			vm->sp[-argc - 1] = OBJ_VAL(newInstance(vm, cls));

			Value ctor;
			if(hashTableGet(&cls->methods, vm->ctor, &ctor)) {
				return callFunction(vm, AS_FUNC(ctor), argc);
			} else if(argc != 0) {
				runtimeError(vm, "Function %s.new() Expected 0 args, but "
						  "instead `%d` supplied.", cls->name->data, argc);
				return false;
			}

			return true;
		}
		default: break;
		}
	}

	ObjClass *cls = getClass(vm, callee);
	runtimeError(vm, "Object %s is not a callable.", cls->name->data);
	return false;
}

static void createClass(VM *vm, ObjString *name, ObjClass *superCls) {
	ObjClass *cls = newClass(vm, name, superCls);

	if(superCls != NULL) {
		hashTableMerge(&cls->methods, &superCls->methods);
	}

	push(vm, OBJ_VAL(cls));
}

static bool invokeMethod(VM *vm, ObjClass *cls, ObjString *name, uint8_t argc) {
	Value method;
	if(!hashTableGet(&cls->methods, name, &method)) {
		runtimeError(vm, "Method %s.%s() doesn't "
			"exists", cls->name->data, name->data);
		return false;
	}

	return callValue(vm, method, argc);
}

static bool invokeFromValue(VM *vm, ObjString *name, uint8_t argc) {
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

			if(!invokeMethod(vm, inst->base.cls, name, argc)) {
				return false;
			}
			return true;
		}
		case OBJ_MODULE: {
			ObjModule *mod = AS_MODULE(val);

			Value func;
			// check if method shadows a function in the module
			if(hashTableGet(&vm->modClass->methods, name, &func)) {
				if(!callValue(vm, func, argc)) {
					return false;
				}
				return true;
			}

			if(!hashTableGet(&mod->globals, name, &func)) {
				runtimeError(vm, "Name `%s` is not defined "
					"in module %s.", name->data, mod->name->data);
				return false;
			}

			if(!callValue(vm, func, argc)) {
				return false;
			}

			return true;
		}
		default: {
			Obj *o = AS_OBJ(val);
			if(!invokeMethod(vm, o->cls, name, argc)) {
				return false;
			}
			return true;
		}
		}
	}

	ObjClass *cls = getClass(vm, val);
	if(!invokeMethod(vm, cls, name, argc)) {
		return false;
	}
	return true;
}

bool getFieldFromValue(VM *vm, Value val, ObjString *name) {
	if(IS_OBJ(val)) {
		switch(OBJ_TYPE(val)) {
		case OBJ_INST: {
			ObjInstance *inst = AS_INSTANCE(val);

			Value v;
			if(!hashTableGet(&inst->fields, name, &v)) {
				//if we didnt find a field try to return bound method
				if(!hashTableGet(&inst->base.cls->methods, name, &v)) {
					runtimeError(vm, "Object %s doesn't have field `%s`.",
									  inst->base.cls->name->data, name->data);
					return false;
				}

				push(vm, OBJ_VAL(newBoundMethod(vm, inst, AS_FUNC(v))));
				return true;
			}

			push(vm, v);
			return true;
		}
		case OBJ_MODULE: {
			ObjModule *mod = AS_MODULE(val);

			Value v;
			if(!hashTableGet(&mod->globals, name, &v)) {
				runtimeError(vm, "Variable `%s` doesn't "
					"exists in module %s", name->data, mod->name->data);
				return false;
			}

			push(vm, v);
			return true;
		}
		default: break;
		}
	}

	ObjClass *cls = getClass(vm, val);
	runtimeError(vm, "Object %s doesn't have field `%s`.", cls->name->data, name->data);
	return false;
}

bool setFieldOfValue(VM *vm, Value val, ObjString *name, Value s) {
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
	runtimeError(vm, "Object %s doesn't have field `%s`.", cls->name->data, name->data);
	return false;
}

static bool isValTrue(Value val) {
	return ((IS_BOOL(val) && AS_BOOL(val))
	      ||(IS_NUM(val) && AS_NUM(val) != 0)
	      ||(IS_STRING(val) && AS_STRING(val)->length != 0)
	      ||(IS_OBJ(val) && !IS_STRING(val)));
}

static ObjString* stringConcatenate(VM *vm, ObjString *s1, ObjString *s2) {
	size_t length = s1->length + s2->length;
	char *data = ALLOC(vm, length + 1);
	memcpy(data, s1->data, s1->length);
	memcpy(data + s1->length, s2->data, s2->length);
	data[length] = '\0';
	return newStringFromBuf(vm, data, length);
}

static bool runEval(VM *vm) {
	Frame *frame = &vm->frames[vm->frameCount - 1];

	#define NEXT_CODE()  (*frame->ip++)
	#define NEXT_SHORT() (frame->ip += 2, ((uint16_t) frame->ip[-2] << 8) | frame->ip[-1])

	#define GET_CONST()  (frame->fn->chunk.consts.arr[NEXT_CODE()])
	#define GET_STRING() (AS_STRING(GET_CONST()))

	#define BINARY(type, op) do { \
		if(!IS_NUM(peek(vm)) || !IS_NUM(peek2(vm))) { \
			runtimeError(vm, "Operands of `%s` must be numbers.", #op); \
			return false; \
		} \
		double b = AS_NUM(pop(vm)); \
		double a = AS_NUM(pop(vm)); \
		push(vm, type(a op b)); \
	} while(0)

	// Eval loop
	for(;;) {

#ifdef DBG_PRINT_EXEC
	printf("     ");
	for(Value *v = vm->stack; v < vm->sp; v++) {
		printf("[");
		printValue(*v);
		printf("]");
	}
	printf("$\n");

	disassembleIstr(&frame->fn->chunk, (size_t) (frame-> ip - frame->fn->chunk.code));
#endif

	if(vm->error) {
		reset(vm);
		return false;
	}

	uint8_t istr;
	switch((istr = NEXT_CODE())) {
	case OP_ADD: {
		if(IS_NUM(peek(vm)) && IS_NUM(peek2(vm))) {
			double b = AS_NUM(pop(vm));
			double a = AS_NUM(pop(vm));
			push(vm, NUM_VAL(a + b));
			continue;
		} else if(IS_STRING(peek(vm)) && IS_STRING(peek2(vm))) {
			ObjString *conc = stringConcatenate(vm, AS_STRING(peek2(vm)),
			                                        AS_STRING(peek(vm)));

			pop(vm);
			pop(vm);

			push(vm, OBJ_VAL(conc));
			continue;
		}
		runtimeError(vm, "Operands of `+` must be two numbers or two strings.");
		return false;
	}
	case OP_MOD: {
		if(!IS_NUM(peek(vm)) || !IS_NUM(peek2(vm))) {
			runtimeError(vm, "Operands of `%` must be numbers.");
			return false;
		}
		double b = AS_NUM(pop(vm));
		double a = AS_NUM(pop(vm));

		if(b == 0) {
			runtimeError(vm, "Modulo by zero error.");
			return false;
		}

		push(vm, NUM_VAL(fmod(a, b)));
		continue;
	}
	case OP_DIV: {
		if(!IS_NUM(peek(vm)) || !IS_NUM(peek2(vm))) {
			runtimeError(vm, "Operands of `/` must be numbers.");
			return false;
		}
		double b = AS_NUM(pop(vm));
		double a = AS_NUM(pop(vm));

		if(b == 0) {
			runtimeError(vm, "Division by zero error.");
			return false;
		}

		push(vm, NUM_VAL(fmod(a, b)));
		continue;
	}
	case OP_SUB: BINARY(NUM_VAL, -);   continue;
	case OP_MUL: BINARY(NUM_VAL, *);   continue;
	case OP_LT:  BINARY(BOOL_VAL, <);  continue;
	case OP_LE:  BINARY(BOOL_VAL, <=); continue;
	case OP_GT:  BINARY(BOOL_VAL, >);  continue;
	case OP_GE:  BINARY(BOOL_VAL, >=); continue;
	case OP_EQ: {
		Value b = pop(vm);
		Value a = pop(vm);
		push(vm, BOOL_VAL(valueEquals(a, b)));
		continue;
	}
	case OP_NEQ: {
		Value b = pop(vm);
		Value a = pop(vm);
		push(vm, BOOL_VAL(!valueEquals(a, b)));
		continue;
	}
	case OP_IS: {
		Value b = pop(vm);
		Value a = pop(vm);

		if(!IS_CLASS(b)) {
			runtimeError(vm, "Right operand of `is` must be a class");
			return false;
		}

		ObjClass *cls = AS_CLASS(b);
		bool isInstance = false;

		ObjClass *c = getClass(vm, a);
		for(;c != NULL; c = c->superCls) {
			if(c == cls) {
				isInstance = true;
				break;
			}
		}

		push(vm, BOOL_VAL(isInstance));
		continue;
	}
	case OP_NEG:
		if(!IS_NUM(peek(vm))) {
			runtimeError(vm, "Operand to `-` must be a number.");
			return false;
		}
		push(vm, NUM_VAL(-AS_NUM(pop(vm))));
		continue;
	case OP_NOT:
		push(vm, BOOL_VAL(!isValTrue(pop(vm))));
		continue;
	case OP_GET_FIELD: {
		Value v = pop(vm);
		if(!getFieldFromValue(vm, v, GET_STRING())) {
			return false;
		}
		continue;
	}
	case OP_SET_FIELD: {
		Value v = pop(vm);
		if(!setFieldOfValue(vm, v, GET_STRING(), peek(vm))) {
			return false;
		}
		continue;
	}
	case OP_ARR_GET: {
		Value i = pop(vm);
		if(!IS_NUM(i)) {
			runtimeError(vm, "Index of array access must be a number.");
			return false;
		}

		double index = AS_NUM(i);
		if((int64_t) index != index) {
			runtimeError(vm, "Index of array access must be an integer.");
			return false;
		}

		Value o = peek(vm);
		if(IS_LIST(o)) {
			ObjList *lst = AS_LIST(o);
			if(index < 0 || index >= lst->count) {
				runtimeError(vm, "List index out of bound: %d.", (int) index);
				return false;
			}

			pop(vm);
			push(vm, lst->arr[(size_t)index]);
		} else if(IS_STRING(o)) {
			ObjString *s = AS_STRING(o);
			if(index < 0 || index >= s->length) {
				runtimeError(vm, "String index out of bound: %d.", (int) index);
				return false;
			}

			char c = s->data[(size_t)index];
			ObjString *strc = copyString(vm, &c, 1);

			pop(vm);
			push(vm, OBJ_VAL(strc));
		} else {
			ObjClass *cls = getClass(vm, o);
			runtimeError(vm, "Operand of get `[]` must be a String or a List, "
					         "instead got %s.", cls->name->data);
			return false;
		}
		continue;
	}
	case OP_ARR_SET: {
		Value i = pop(vm);
		if(!IS_NUM(i)) {
			runtimeError(vm, "Index of array access must be a number.");
			return false;
		}

		double index = AS_NUM(i);
		if((int64_t) index != index) {
			runtimeError(vm, "Index of array access must be an integer.");
			return false;
		}

		Value o = pop(vm);
		if(IS_LIST(o)) {
			ObjList *lst = AS_LIST(o);
			if(index < 0 || index >= lst->count) {
				runtimeError(vm, "List index out of bound: %d.", (int) index);
				return false;
			}

			lst->arr[(size_t)index] = peek(vm);
		} else {
			runtimeError(vm, "Operand of set `[]` must be a list.");
			return false;
		}
		continue;
	}
	case OP_JUMP: {
		int16_t off = NEXT_SHORT();
		frame->ip += off;
		continue;
	}
	case OP_JUMPF: {
		int16_t off = NEXT_SHORT();
		if(!isValTrue(pop(vm))) frame->ip += off;
		continue;
	}
	case OP_JUMPT: {
		int16_t off = NEXT_SHORT();
		if(isValTrue(pop(vm))) frame->ip += off;
		continue;
	}
	case OP_NULL:
		push(vm, NULL_VAL);
		continue;
	case OP_CALL: {
		uint8_t argc = NEXT_CODE();
		goto call;
	case OP_CALL_0:
	case OP_CALL_1:
	case OP_CALL_2:
	case OP_CALL_3:
	case OP_CALL_4:
	case OP_CALL_5:
	case OP_CALL_6:
	case OP_CALL_7:
	case OP_CALL_8:
	case OP_CALL_9:
	case OP_CALL_10:
		argc = istr - OP_CALL_0;
call:
		if(!callValue(vm, peekn(vm, argc), argc)) {
			return false;
		}
		frame = &vm->frames[vm->frameCount - 1];
		continue;
	}
	case OP_INVOKE: {
		uint8_t argc = NEXT_CODE();
		goto invoke;
	case OP_INVOKE_0:
	case OP_INVOKE_1:
	case OP_INVOKE_2:
	case OP_INVOKE_3:
	case OP_INVOKE_4:
	case OP_INVOKE_5:
	case OP_INVOKE_6:
	case OP_INVOKE_7:
	case OP_INVOKE_8:
	case OP_INVOKE_9:
	case OP_INVOKE_10:
		argc = istr - OP_INVOKE_0;
invoke:
		if(!invokeFromValue(vm, GET_STRING(), argc)) {
			return false;
		}
		frame = &vm->frames[vm->frameCount - 1];
		continue;
	}
	case OP_SUPER: {
		uint8_t argc = NEXT_CODE();
		goto sup_invoke;
	case OP_SUPER_0:
	case OP_SUPER_1:
	case OP_SUPER_2:
	case OP_SUPER_3:
	case OP_SUPER_4:
	case OP_SUPER_5:
	case OP_SUPER_6:
	case OP_SUPER_7:
	case OP_SUPER_8:
	case OP_SUPER_9:
	case OP_SUPER_10:
		argc = istr - OP_SUPER_0;
sup_invoke:;
		ObjInstance *inst = AS_INSTANCE(peekn(vm, argc));
		if(!invokeMethod(vm, inst->base.cls->superCls, GET_STRING(), argc)) {
			return false;
		}
		frame = &vm->frames[vm->frameCount - 1];
		continue;
	}
	case OP_RETURN: {
		Value ret = pop(vm);

		vm->frameCount--;
		if(vm->frameCount == 0) {
			return true;
		}

		vm->sp = frame->stack;
		push(vm, ret);

		frame = &vm->frames[vm->frameCount - 1];
		vm->module = frame->fn->module;
		continue;
	}
	case OP_IMPORT_AS: {
	case OP_IMPORT:;
		ObjString *name = GET_STRING();
		if(!importModule(vm, name)) {
			runtimeError(vm, "Cannot load module `%s`", name->data);
			return false;
		}

		//define name for the module in the importing module
		hashTablePut(&vm->module->globals, istr == OP_IMPORT ?
					name : GET_STRING(), OBJ_VAL(getModule(vm, name)));

		//call the module's main if first time import
		if(!valueEquals(peek(vm), NULL_VAL)) {
			callValue(vm, peek(vm), 0);
			frame = &vm->frames[vm->frameCount - 1];
		}
		continue;
	}
	case OP_APPEND_LIST: {
		ObjList *l = AS_LIST(peek2(vm));

		listAppend(vm, l, peek(vm));
		pop(vm);
		continue;
	}
	case OP_NEW_LIST:
		push(vm, OBJ_VAL(newList(vm, 0)));
		continue;
	case OP_NEW_CLASS:
		createClass(vm, GET_STRING(), vm->objClass);
		continue;
	case OP_NEW_SUBCLASS:
		if(!IS_CLASS(peek(vm))) {
			runtimeError(vm, "Superclass in class declaration must be a Class.");
			return false;
		}
		createClass(vm, GET_STRING(), AS_CLASS(pop(vm)));
		continue;
	case OP_DEF_METHOD: {
		ObjClass *cls = AS_CLASS(peek(vm));
		ObjString *methodName = GET_STRING();
		hashTablePut(&cls->methods, methodName, GET_CONST());
		continue;
	}
	case OP_NAT_METHOD: {
		ObjClass *cls = AS_CLASS(peek(vm));
		ObjString *methodName = GET_STRING();
		ObjNative *native = AS_NATIVE(GET_CONST());

		native->fn = resolveBuiltIn(vm->module->name->data, cls->name->data, methodName->data);
		if(native->fn == NULL) {
			runtimeError(vm, "Cannot resolve native method %s().", native->name->data);
			return false;
		}

		hashTablePut(&cls->methods, methodName, OBJ_VAL(native));
		continue;
	}
	case OP_DEFINE_NATIVE: {
		ObjString *name = GET_STRING();
		ObjNative *nat  = AS_NATIVE(pop(vm));

		nat->fn = resolveBuiltIn(vm->module->name->data, NULL, name->data);
		if(nat->fn == NULL) {
			runtimeError(vm, "Cannot resolve native %s.", nat->name->data);
			return false;
		}

		hashTablePut(&vm->module->globals, name, OBJ_VAL(nat));
		continue;
	}
	case OP_GET_CONST:
		push(vm, GET_CONST());
		continue;
	case OP_DEFINE_GLOBAL:
		hashTablePut(&vm->module->globals, GET_STRING(), pop(vm));
		continue;
	case OP_GET_GLOBAL: {
		ObjString *name = GET_STRING();
		if(!hashTableGet(&vm->module->globals, name, vm->sp++)) {
			runtimeError(vm, "Name `%s` is not defined.", name->data);
			return false;
		}
		continue;
	}
	case OP_SET_GLOBAL: {
		ObjString *name = GET_STRING();
		if(hashTablePut(&vm->module->globals, name, peek(vm))) {
			runtimeError(vm, "Name `%s` is not defined.", name->data);
			return false;
		}
		continue;
	}
	case OP_GET_LOCAL:
		push(vm, frame->stack[NEXT_CODE()]);
		continue;
	case OP_SET_LOCAL:
		frame->stack[NEXT_CODE()] = peek(vm);
		continue;
	case OP_POP:
		pop(vm);
		continue;
	case OP_DUP:
		*vm->sp = *(vm->sp - 1);
		vm->sp++;
		continue;
	}

	}

	#undef NEXT_CODE
	#undef NEXT_SHORT
	#undef GET_CONST
	#undef GET_STRING
	#undef BINARY
}

EvalResult evaluate(VM *vm, const char *src) {
	return evaluateModule(vm, "__main__", src);
}

EvalResult evaluateModule(VM *vm, const char *module, const char *src) {
	Parser p;

	Stmt *program = parse(&p, src);
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

static void runtimeError(VM *vm, const char* format, ...) {
	fprintf(stderr, "Traceback:\n");

	for(int i = 0; i < vm->frameCount; i++) {
		Frame *frame = &vm->frames[i];
		ObjFunction *func = frame->fn;
		size_t istr = frame->ip - func->chunk.code - 1;
		fprintf(stderr, "    [line:%d] ", getBytecodeSrcLine(&func->chunk, istr));

		fprintf(stderr, "module %s in ", func->module->name->data);

		if(func->name != NULL) {
			fprintf(stderr, "%s()\n", func->name->data);
		} else {
			fprintf(stderr, "<main>\n");
		}

	}

	va_list args;
	va_start(args, format);
	vfprintf(stderr, format, args);
	va_end(args);
	fprintf(stderr, "\n");

	reset(vm);
}

void initCommandLineArgs(int argc, const char **argv) {
	sysInitArgs(argc, argv);
}

void freeVM(VM *vm) {
	reset(vm);

	freeHashTable(&vm->strings);
	freeHashTable(&vm->modules);
	freeObjects(vm);

	free(vm->stack);
	free(vm->frames);

#ifdef DBG_PRINT_GC
	printf("Allocated at exit: %lu bytes.\n", vm->allocated);
#endif
}
