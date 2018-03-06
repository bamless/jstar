#include "compiler.h"
#include "ast.h"
#include "vm.h"
#include "value.h"
#include "memory.h"
#include "opcode.h"

#include <stdio.h>
#include <string.h>

void initCompiler(Compiler *c, Compiler *enclosing, VM *vm) {
	c->vm = vm;
	c->enclosing = enclosing;
	c->localsCount = 0;
	c->depth = 0;
	c->func = NULL;
}

static void error(const char *msg) {
	fprintf(stderr, "%s\n", msg);
	exit(100);
}

static size_t emitByteCode(Compiler *c, uint8_t b, int line) {
	return writeByte(&c->func->chunk, b, line);
}

static uint8_t createConstant(ObjFunction *f, Value c) {
	ValueArray *consts = &f->chunk.consts;

	if(consts->count > UINT8_MAX)
		error("too many constants in func"); //TODO: add func name

	for(size_t i = 0; i < consts->count; i++) {
		if(consts->arr[i] == c) {
			return i;
		}
	}

	return valueArrayAppend(&f->chunk.consts, c);
}

static uint8_t identifierConst(Compiler *c, Identifier *id) {
	ObjString *idStr = copyString(&c->vm->mem,  id->name, id->length);
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
	c->func = newFunction(&c->vm->mem, 0);

	LinkedList *stmts = p->stmts;
	while(stmts != NULL) {
		Stmt *s = (Stmt*)stmts->elem;

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

void reachCompilerRoots(MemManager *m, Compiler *c) {
	while(c != NULL) {
		reachObject(m, (Obj*) c->func);
		c = c->enclosing;
	}
}
