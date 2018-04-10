#include "vm.h"
#include "ast.h"
#include "parser.h"
#include "opcode.h"
#include "disassemble.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <float.h>
#include <math.h>

#define INIT_GC 1024 * 1024 // 1MiB

static void runtimeError(VM *vm, const char* format, ...);

static void reset(VM *vm) {
	vm->sp = vm->stack;
	vm->frameCount = 0;
}

void initVM(VM *vm) {
	vm->currCompiler = NULL;

	vm->stack  = malloc(sizeof(Value) * STACK_SZ);
	vm->frames = malloc(sizeof(Frame) * FRAME_SZ);

	reset(vm);

	initHashTable(&vm->globals);
	initHashTable(&vm->strings);

	vm->nextGC = INIT_GC;
	vm->objects = NULL;
	vm->disableGC = false;

	vm->allocated = 0;
	vm->reachedStack = NULL;
	vm->reachedCapacity = 0;
	vm->reachedCount = 0;
}

static bool callFunction(VM *vm, ObjFunction *func, uint16_t argc) {
	if(func->argsCount != argc) {
		runtimeError(vm, "Function `%s` expexted %d args, but instead %d supplied.",
			func->name->data, func->argsCount, argc);
		return false;
	}

	if(vm->frameCount == FRAME_SZ) {
		runtimeError(vm, "Stack overflow.");
		return false;
	}

	Frame *callFrame = &vm->frames[vm->frameCount++];
	callFrame->fn = func;
	callFrame->ip = func->chunk.code;
	callFrame->stack = vm->sp - (argc + 1);

	return true;
}

static bool callValue(VM *vm, Value callee, uint16_t argc) {
	if(IS_OBJ(callee)) {
		switch(OBJ_TYPE(callee)) {
		case OBJ_FUNCTION:
			return callFunction(vm, AS_FUNC(callee), argc);
			break;
		default: break;
		}
	}

	runtimeError(vm, "Can only call function and native objects.");
	return false;
}

#define BINARY(type, op) do { \
	if(!IS_NUM(peek(vm)) || !IS_NUM(peek2(vm))) { \
		runtimeError(vm, "Operands must be numbers."); \
		return false; \
	} \
	double b = AS_NUM(pop(vm)); \
	double a = AS_NUM(pop(vm)); \
	push(vm, type(a op b)); \
} while(0)

static bool isValTrue(Value val) {
	if(IS_BOOL(val)) {
		return AS_BOOL(val);
	} else if(IS_NULL(val)) {
		return false;
	} else if(IS_NUM(val)) {
		return AS_NUM(val) != 0;
	} else if(IS_STRING(val)) {
		return AS_STRING(val)->length != 0;
	} else {
		return true;
	}
}

static ObjString* stringConcatenate(ObjString *s1, ObjString *s2) {
	return NULL;
}

static bool runEval(VM *vm) {
	Frame *frame = &vm->frames[vm->frameCount - 1];

	#define NEXT_CODE()  (*frame->ip++)
	#define NEXT_SHORT() (frame->ip += 2, ((uint16_t) frame->ip[-2] << 8) | frame->ip[-1])

	#define GET_CONST()  (frame->fn->chunk.consts.arr[NEXT_CODE()])
	#define GET_STRING() (AS_STRING(GET_CONST()))

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

	uint8_t istr;
	switch((istr = NEXT_CODE())) {
	case OP_ADD: {
		if(IS_NUM(peek(vm)) && IS_NUM(peek2(vm))) {
			double b = AS_NUM(pop(vm));
			double a = AS_NUM(pop(vm));
			push(vm, NUM_VAL(a + b));
			continue;
		} else if(IS_STRING(peek(vm)) && IS_STRING(peek2(vm))) {
			ObjString *s2 = AS_STRING(pop(vm));
			ObjString *s1 = AS_STRING(pop(vm));
			push(vm, OBJ_VAL(stringConcatenate(s1, s2)));
			continue;
		}
		runtimeError(vm, "Operands of `+` must be two numbers of rwo strings.");
		return false;
	}
	case OP_MOD: {
		if(!IS_NUM(peek(vm)) || !IS_NUM(peek2(vm))) {
			runtimeError(vm, "Operands must be numbers.");
			return false;
		}
		double b = AS_NUM(pop(vm));
		double a = AS_NUM(pop(vm));
		push(vm, NUM_VAL(fmod(a, b)));
		continue;
	}
	case OP_SUB: BINARY(NUM_VAL, -);   continue;
	case OP_MUL: BINARY(NUM_VAL, *);   continue;
	case OP_DIV: BINARY(NUM_VAL, /);   continue;
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
	case OP_NEG: {
		if(!IS_NUM(peek(vm))) {
			runtimeError(vm, "Operand to `-` must be a number");
			return false;
		}
		double n = -AS_NUM(pop(vm));
		push(vm, NUM_VAL(n));
		continue;
	}
	case OP_NOT: {
		bool v = !isValTrue(pop(vm));
		push(vm, BOOL_VAL(v));
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
		int8_t argc = NEXT_CODE();
		if(!callValue(vm, peekn(vm, argc), argc)) {
			return false;
		}
		frame = &vm->frames[vm->frameCount - 1];
		continue;
	}
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
	case OP_CALL_10: {
		int8_t argc = istr - OP_CALL_0;
		if(!callValue(vm, peekn(vm, argc), argc)) {
			return false;
		}
		frame = &vm->frames[vm->frameCount - 1];
		continue;
	}
	case OP_RETURN: {
		*frame->stack = pop(vm);

		vm->frameCount--;
		vm->sp = frame->stack + 1;
		frame = &vm->frames[vm->frameCount - 1];

		continue;
	}
	case OP_GET_CONST:
		push(vm, GET_CONST());
		continue;
	case OP_DEFINE_GLOBAL:
		hashTablePut(&vm->globals, GET_STRING(), pop(vm));
		continue;
	case OP_GET_GLOBAL: {
		ObjString *name = GET_STRING();
		if(!hashTableGet(&vm->globals, name, vm->sp++)) {
			runtimeError(vm, "Variable `%s` not defined.", name->data);
			return false;
		}
		continue;
	}
	case OP_SET_GLOBAL: {
		ObjString *name = GET_STRING();
		if(hashTablePut(&vm->globals, name, peek(vm))) {
			runtimeError(vm, "Variable `%s` not defined.", name->data);
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
	case OP_PRINT:
		printValue(pop(vm));
		printf("\n");
		continue;
	case OP_POP:
		(void) pop(vm);
		continue;
	case OP_DUP:
		*vm->sp = *(vm->sp - 1);
		vm->sp++;
		continue;
	case OP_HALT:
		reset(vm);
		return true;
	}

	}
}

EvalResult evaluate(VM *vm, const char *src) {
	Parser p;
	Compiler c;

	Stmt *program = parse(&p, src);
	if(p.hadError) {
		freeStmt(program);
		return VM_SYNTAX_ERR;
	}

	initCompiler(&c, NULL, 0, vm);
	ObjFunction *fn = compile(&c, program);
	endCompiler(&c);

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
	va_list args;
	va_start(args, format);
	vfprintf(stderr, format, args);
	va_end(args);
	fprintf(stderr, "\n");

	fprintf(stderr, "Traceback:\n");

	for(int i = vm->frameCount - 1; i >= 0; i--) {
		Frame *frame = &vm->frames[i];
		ObjFunction *func = frame->fn;
		size_t istr = frame->ip - func->chunk.code - 1;
		fprintf(stderr, "    [line:%d] in ", getBytecodeSrcLine(&func->chunk, istr));

		if(func->name != NULL) {
			fprintf(stderr, "%s()\n", func->name->data);
		} else {
			fprintf(stderr, "<main>\n");
		}
	}

	reset(vm);
}

void freeVM(VM *vm) {
	reset(vm);

	freeHashTable(&vm->globals);
	freeHashTable(&vm->strings);
	freeObjects(vm);

	free(vm->stack);
	free(vm->frames);

#ifdef DBG_PRINT_GC
	printf("Allocated at exit: %lu bytes.\n", vm->allocated);
#endif
}
