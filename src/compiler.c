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

static size_t emitBytecode(Compiler *c, uint8_t b, int line) {
	return writeByte(&c->func->chunk, b, line);
}

static size_t emitShort(Compiler *c, uint16_t s, int line) {
	size_t i = writeByte(&c->func->chunk, (uint8_t) (s >> 8), line);
	writeByte(&c->func->chunk, (uint8_t) s, line);
	return i;
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

static int resolveVariable(Compiler *c, Identifier *id) {
	for(int i = c->localsCount - 1; i >= 0; i--) {
		if(identifierEquals(&c->locals[i].id, id)) {
			if (c->locals[i].depth == -1) {
				error("Cannot read local variable in its own initializer");
			}
			return i;
		}
	}
	return -1;
}

static void addLocal(Compiler *c, Identifier *id) {
	if(c->localsCount == MAX_LOCALS) {
		error("too many local variables in function %s", c->func->name->data);
	}
	Local *local = &c->locals[c->localsCount];
	local->id = *id;
	local->depth = -1;
	c->localsCount++;
}

static void declareVar(Compiler *c, Identifier *id) {
	for(int i = c->localsCount - 1; i >= 0; i--) {
		if(c->locals[i].depth != -1 && c->locals[i].depth < c->depth) break;
		if(identifierEquals(&c->locals[i].id, id)) {
			error("Variable %.*s already declared", id->length, id->name);
		}
	}

	addLocal(c, id);
}

static size_t emitJumpTo(Compiler *c, int jmpOpcode, size_t target) {
	int32_t offset = target - (c->func->chunk.count + 2);
	if(offset > INT16_MAX || offset < INT16_MIN) {
		error("Too much code to jump");
	}

	Chunk *chunk = &c->func->chunk;
	emitBytecode(c, jmpOpcode, chunk->lines[chunk->linesCount]);
	emitShort(c, (uint16_t) offset, chunk->lines[chunk->linesCount]);
	return c->func->chunk.count - 2;
}

static void setJumpTo(Compiler *c, size_t jumpAddr, size_t target) {
	int32_t offset = target - (jumpAddr + 2);
	if(offset > INT16_MAX || offset < INT16_MIN) {
		error("Too much code to jump");
	}

	Chunk *chunk = &c->func->chunk;
	chunk->code[jumpAddr + 1] = (uint8_t) (uint16_t) offset >> 8;
	chunk->code[jumpAddr + 2] = (uint8_t) (uint16_t) offset;
}

static void function(Compiler *c, Stmt *s) {
	Compiler funComp;
	initCompiler(&funComp, c, c->depth + 1, c->vm);

	ObjFunction *fn = compileFunction(&funComp, s);
	uint8_t fnIndex = createConstant(c->func, OBJ_VAL(fn));
	uint8_t idIndex = identifierConst(c, &s->funcDecl.id);

	emitBytecode(c, OP_GET_CONST, s->line);
	emitBytecode(c, fnIndex, s->line);
	emitBytecode(c, OP_DEFINE_GLOBAL, s->line);
	emitBytecode(c, idIndex, s->line);

	endCompiler(&funComp);
}

static void compileExpr(Compiler *c, Expr *e);

static void compileBinaryExpr(Compiler *c, Expr *e) {
	compileExpr(c, e->bin.left);
	compileExpr(c, e->bin.right);
	switch(e->bin.op) {
	case PLUS:  emitBytecode(c, OP_ADD, e->line);  break;
	case MINUS: emitBytecode(c, OP_SUB, e->line);  break;
	case MULT:  emitBytecode(c, OP_MULT, e->line); break;
	case DIV:   emitBytecode(c, OP_DIV, e->line);  break;
	case MOD:   emitBytecode(c, OP_MOD, e->line);  break;
	case EQ:    emitBytecode(c, OP_EQ, e->line);   break;
	case NEQ:   emitBytecode(c, OP_NEQ, e->line);  break;
	case AND:   emitBytecode(c, OP_AND, e->line);  break;
	case OR:    emitBytecode(c, OP_OR, e->line);   break;
	case GT:    emitBytecode(c, OP_GT, e->line);   break;
	case GE:    emitBytecode(c, OP_GE, e->line);   break;
	case LT:    emitBytecode(c, OP_LT, e->line);   break;
	case LE:    emitBytecode(c, OP_LE, e->line);   break;
	default:
		error("Wrong operator for binary expression");
		break;
	}
}

static void compileUnaryExpr(Compiler *c, Expr *e) {
	compileExpr(c, e->unary.operand);
	switch(e->unary.op) {
	case NOT: emitBytecode(c, OP_NOT, e->line); break;
	default:
		error("Wrong operator for unary expression");
		break;
	}
}

static void compileAssignExpr(Compiler *c, Expr *e) {
	compileExpr(c, e->assign.rval);
	int i = resolveVariable(c, &e->assign.lval->var.id);
	if(i != -1) {
		emitBytecode(c, OP_SET_LOCAL, e->line);
		emitBytecode(c, (uint8_t) i, e->line);
	} else {
		emitBytecode(c, OP_SET_GLOBAL, e->line);
		emitBytecode(c, identifierConst(c, &e->assign.lval->var.id), e->line);
	}
}

static void compileCallExpr(Compiler *c, Expr *e) {
	LinkedList *n;
	uint8_t argsc = 0;
	foreach(n, e->callExpr.args->exprList.lst) {
		if(argsc == UINT8_MAX) {
			error("Too many arguments for function %s", c->func->name->data);
		}

		argsc++;
		compileExpr(c, (Expr*) n->elem);
	}

	compileExpr(c, e->callExpr.callee);

	emitBytecode(c, OP_CALL, e->line);
	emitBytecode(c, argsc, e->line);
}

static void compileExpr(Compiler *c, Expr *e) {
	switch(e->type) {
	case ASSIGN:
	 	compileAssignExpr(c, e);
		break;
	case BINARY:
		compileBinaryExpr(c, e);
		break;
	case UNARY:
		compileUnaryExpr(c, e);
		break;
	case CALL_EXPR:
		compileCallExpr(c, e);
		break;
	case EXPR_LST: {
		LinkedList *lst = e->exprList.lst;
		while(lst != NULL) {
			compileExpr(c, (Expr*) lst->elem);
		}
		break;
	}
	case NUM_LIT:
		emitBytecode(c, OP_GET_CONST, e->line);
		emitBytecode(c, createConstant(c->func, NUM_VAL(e->num)), e->line);
		break;
	case BOOL_LIT:
		emitBytecode(c, OP_GET_CONST, e->line);
		emitBytecode(c, createConstant(c->func, BOOL_VAL(e->boolean)), e->line);
		break;
	case STR_LIT: {
		emitBytecode(c, OP_GET_CONST, e->line);
		ObjString *str = copyString(c->vm, e->str.str + 1, e->str.length - 2);
		emitBytecode(c, createConstant(c->func, OBJ_VAL(str)), e->line);
		break;
	}
	case VAR_LIT: {
		int i = resolveVariable(c, &e->var.id);
		if(i != -1) {
			emitBytecode(c, OP_GET_LOCAL, e->line);
			emitBytecode(c, (uint8_t) i, e->line);
		} else {
			emitBytecode(c, OP_GET_GLOBAL, e->line);
			emitBytecode(c, identifierConst(c, &e->var.id), e->line);
		}
		break;
	}
	case NULL_LIT:
		emitBytecode(c, OP_NULL, e->line);
		break;
	}
}

static void compileVarDecl(Compiler *c, Stmt *s) {
	if(c->depth != 0) {
		declareVar(c, &s->varDecl.id);
	}

	if(s->varDecl.init != NULL) {
		compileExpr(c, s->varDecl.init);
	} else {
		emitBytecode(c, OP_NULL, s->line);
	}

	if(c->depth == 0) {
		uint8_t i = identifierConst(c, &s->varDecl.id);
		emitBytecode(c, OP_DEFINE_GLOBAL, s->line);
		emitBytecode(c, i, s->line);
		emitBytecode(c, OP_SET_GLOBAL, s->line);
		emitBytecode(c, i, s->line);
	} else {
		c->locals[c->localsCount - 1].depth = c->depth;
		emitBytecode(c, OP_SET_LOCAL, s->line);
		emitBytecode(c, c->localsCount - 1, s->line);
	}

	emitBytecode(c, OP_POP, s->line);
}

static void compileStatement(Compiler *c, Stmt *s);
static void compileStatements(Compiler *c, LinkedList *stmts);

static void compileReturn(Compiler *c, Stmt *s) {
	if(c->depth == 0) {
		error("Cannot use return in global scope.");
	}

	if(s->returnStmt.e != NULL) {
		compileExpr(c, s->returnStmt.e);
	} else {
		emitBytecode(c, OP_NULL, s->line);
	}
	emitBytecode(c, OP_RETURN, s->line);
}

static void compileWhileStatement(Compiler *c, Stmt *s) {
	size_t start = c->func->chunk.count;

	compileExpr(c, s->whileStmt.cond);

	size_t exitJmp = emitBytecode(c, OP_JUMPF, s->line);
	emitShort(c, 0, s->line);

	compileStatement(c, s->whileStmt.body);

	emitJumpTo(c, OP_JUMP, start);

	setJumpTo(c, exitJmp, c->func->chunk.count);
}

static void compileStatement(Compiler *c, Stmt *s) {
	//here generate code
	switch(s->type) {
	case IF: break;
	case FOR: break;
	case WHILE:
		compileWhileStatement(c, s);
		break;
	case BLOCK:
		enterScope(c);
		compileStatements(c, s->blockStmt.stmts);
		exitScope(c);
		break;
	case RETURN:
		compileReturn(c, s);
		break;
	case EXPR:
		compileExpr(c, s->exprStmt);
		emitBytecode(c, OP_POP, s->line);
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
		compileStatement(c, (Stmt*) stmts->elem);
		stmts = stmts->next;
	}
}

ObjFunction *compile(Compiler *c, Stmt *s) {
	c->func = newFunction(c->vm, 0);
	compileStatements(c, s->blockStmt.stmts);
	emitBytecode(c, OP_HALT, s->line);
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

	emitBytecode(c, OP_NULL, s->line);
	emitBytecode(c, OP_RETURN, s->line);
	return c->func;
}

void reachCompilerRoots(VM *vm, Compiler *c) {
	while(c != NULL) {
		reachObject(vm, (Obj*) c->func);
		c = c->prev;
	}
}
