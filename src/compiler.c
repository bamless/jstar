#include "compiler.h"
#include "ast.h"
#include "vm.h"
#include "value.h"
#include "memory.h"
#include "opcode.h"

#include <stdio.h>
#include <string.h>
#include <stdarg.h>

ObjFunction *compileFunction(Compiler *c, Stmt *s);

void initCompiler(Compiler *c, Compiler *prev, int depth, VM *vm) {
	c->vm = vm;
	c->prev = prev;
	c->localsCount = 0;
	c->depth = depth;
	c->func = NULL;
	vm->currCompiler = c;
}

void endCompiler(Compiler *c) {
	c->vm->currCompiler = c->prev;
}

static void error(const char *format, ...) {
	va_list args;
	va_start(args, format);
	vfprintf(stderr, format, args);
	va_end(args);
	fputs("\n", stderr);
	exit(EXIT_FAILURE);
}

static void enterScope(Compiler *c) {
	c->depth++;
}

static void exitScope(Compiler *c) {
	c->depth--;
	while(c->localsCount > 0 && c->locals[c->localsCount - 1].depth > c->depth) {
		c->localsCount--;
	}
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
	ObjString *idStr = copyString(c->vm, id->name, id->length);
	return createConstant(c->func, OBJ_VAL(idStr));
}

static void addLocal(Compiler *c, Identifier *id) {
	if(c->localsCount == MAX_LOCALS) {
		error("too many local variables in function %s", c->func->name->data);
	}
	Local *local = &c->locals[c->localsCount];
	local->id = *id;
	local->depth = c->depth;
	c->localsCount++;
}

static void declareVar(Compiler *c, Identifier *id) {
	for(int i = c->localsCount - 1; i >= 0; i--) {
		if(c->locals[i].depth < c->depth) break;
		if(identifierEquals(&c->locals[i].id, id)) {
			error("Variable %.*s already declared", id->length, id->name);
		}
	}

	addLocal(c, id);
}

static void function(Compiler *c, Stmt *s) {
	Compiler funComp;
	initCompiler(&funComp, c, c->depth + 1, c->vm);

	ObjFunction *fn = compileFunction(&funComp, s);
	uint8_t fnIndex = createConstant(c->func, OBJ_VAL(fn));
	uint8_t idIndex = identifierConst(c, &s->funcDecl.id);

	emitByteCode(c, OP_GET_CONST, s->line);
	emitByteCode(c, fnIndex, s->line);
	emitByteCode(c, OP_DEFINE_GLOBAL, s->line);
	emitByteCode(c, idIndex, s->line);

	endCompiler(&funComp);
}

static void compileVarDecl(Compiler *c, Stmt *s) {
	if(c->depth == 0) {
		uint8_t i = identifierConst(c, &s->varDecl.id);
		emitByteCode(c, OP_DEFINE_GLOBAL, s->line);
		emitByteCode(c, i, s->line);
	} else {
		declareVar(c, &s->varDecl.id);
	}
}

static void compileReturn(Compiler *c, Stmt *s) {
	if(c->depth == 0) {
		error("Cannot use return in global scope.");
	}

	if(s->returnStmt.e != NULL) {
		//here compile expression
	} else {
		emitByteCode(c, OP_NULL, s->line);
	}
	emitByteCode(c, OP_RETURN, s->line);
}

static void compileStatements(Compiler *c, LinkedList *stmts);

static void compileStatement(Compiler *c, Stmt *s) {
	//here generate code
	switch(s->type) {
	case IF: break;
	case FOR: break;
	case WHILE: break;
	case BLOCK:
		enterScope(c);
		compileStatements(c, s->blockStmt.stmts);
		exitScope(c);
		break;
	case RETURN:
		compileReturn(c, s);
		break;
	case EXPR:
		//compile Expressions
		emitByteCode(c, OP_POP, s->line);
		break;
	case VARDECL:
		compileVarDecl(c, s);
		break;
	case FUNCDECL:
		function(c, s);
		break;
	}
}

static void compileStatements(Compiler *c, LinkedList *stmts) {
	while(stmts != NULL) {
		compileStatement(c, (Stmt *) stmts->elem);
		stmts = stmts->next;
	}
}

ObjFunction *compile(Compiler *c, Stmt *s) {
	c->func = newFunction(c->vm, 0);
	compileStatements(c, s->blockStmt.stmts);
	emitByteCode(c, OP_HALT, s->line);
	return c->func;
}

ObjFunction *compileFunction(Compiler *c, Stmt *s) {
	c->func = newFunction(c->vm, linkedListLength(s->funcDecl.formalArgs));
	c->func->name = copyString(c->vm, s->funcDecl.id.name, s->funcDecl.id.length);

	enterScope(c);
	LinkedList *formals = s->funcDecl.formalArgs;
	while(formals != NULL) {
		declareVar(c, (Identifier*) formals->elem);
		formals = formals->next;
	}
	compileStatements(c, s->funcDecl.body->blockStmt.stmts);
	exitScope(c);

	emitByteCode(c, OP_NULL, s->line);
	emitByteCode(c, OP_RETURN, s->line);
	return c->func;
}

void reachCompilerRoots(VM *vm, Compiler *c) {
	while(c != NULL) {
		reachObject(vm, (Obj*) c->func);
		c = c->prev;
	}
}
