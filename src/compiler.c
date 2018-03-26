#include "compiler.h"
#include "ast.h"
#include "vm.h"
#include "value.h"
#include "memory.h"
#include "opcode.h"

#include <stdio.h>
#include <string.h>
#include <stdarg.h>

void initCompiler(Compiler *c, Compiler *prev, VM *vm, bool topOfFile) {
	c->vm = vm;
	c->topOfFile = topOfFile;
	c->prev = prev;
	c->localsCount = 0;
	c->depth = 0;
	c->func = NULL;
}

static void error(const char *format, ...) {
	va_list args;
	va_start(args, format);
	vfprintf(stderr, format, args);
	va_end(args);
	fputs("\n", stderr);
	exit(EXIT_FAILURE);
}

static size_t emitByteCode(Compiler *c, uint8_t b, int line) {
	return writeByte(&c->func->chunk, b, line);
}

static uint8_t createConstant(ObjFunction *f, Value c) {
	int index = addConstant(&f->chunk, c);
	if(index == -1) {
		error("too many constants in function %s", f->name->data);
	}
	return (uint8_t) index;
}

static uint8_t identifierConst(Compiler *c, Identifier *id) {
	ObjString *idStr = copyString(c->vm,  id->name, id->length);
	return createConstant(c->func, OBJ_VAL(idStr));
}

static void compileVarDecl(Compiler *c, Stmt *s) {
	uint8_t i = identifierConst(c, &s->varDecl.id);

	if(c->depth == 0) {
		emitByteCode(c, OP_DEFINE_GLOBAL, s->line);
		emitByteCode(c, i, s->line);
	}
}

ObjFunction *compile(Compiler *c, Program *p) {
	c->func = newFunction(c->vm, 0);

	LinkedList *stmts = p->stmts;
	while(stmts != NULL) {
		Stmt *s = (Stmt*) stmts->elem;

		//here generate code
		switch(s->type) {
		case IF: break;
		case FOR: break;
		case WHILE: break;
		case BLOCK: break;
		case RETURN: break;
		case EXPR: break;
		case VARDECL:
			compileVarDecl(c, s);
			break;
		case FUNCDECL: break;
		}
	}

	return c->func;
}

void reachCompilerRoots(VM *vm, Compiler *c) {
	while(c != NULL) {
		reachObject(vm, (Obj*) c->func);
		c = c->prev;
	}
}
