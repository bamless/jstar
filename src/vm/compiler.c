#include "compiler.h"
#include "memory.h"
#include "opcode.h"
#include "value.h"
#include "vm.h"

#include "util/stringbuf.h"

#include "parse/ast.h"

#include <stdio.h>
#include <string.h>
#include <stdarg.h>

typedef struct Local {
	Identifier id;
	int depth;
} Local;

typedef enum FuncType {
	TYPE_FUNC, TYPE_METHOD, TYPE_CTOR
} FuncType;

typedef struct Compiler {
	BlangVM *vm;
	Compiler *prev;

	bool hasSuper;

	int loopDepth;

	FuncType type;
	ObjFunction *func;

	uint8_t localsCount;
	Local locals[MAX_LOCALS];

	bool hadError;
	int depth;

	int tryDepth;
} Compiler;

static ObjFunction *function(Compiler *c, ObjModule *module, Stmt *s);
static ObjFunction *method(Compiler *c, ObjModule *module, Identifier *classId, Stmt *s);

static void initCompiler(Compiler *c, Compiler *prev, FuncType t, int depth, BlangVM *vm) {
	c->vm = vm;
	c->type = t;
	c->func = NULL;
	c->prev = prev;
	c->tryDepth = 0;
	c->loopDepth = 0;
	c->depth = depth;
	c->localsCount = 0;
	c->hasSuper = false;
	c->hadError = false;

	vm->currCompiler = c;
}

static void endCompiler(Compiler *c) {
	if(c->prev != NULL)  {
		c->prev->hadError |= c->hadError;
	}
	c->vm->currCompiler = c->prev;
}

static void error(Compiler *c, int line, const char *format, ...) {
	fprintf(stderr, "[line:%d] ", line);
	va_list args;
	va_start(args, format);
	vfprintf(stderr, format, args);
	va_end(args);
	fprintf(stderr, "\n");
	c->hadError = true;
}

static size_t emitBytecode(Compiler *c, uint8_t b, int line) {
	if(line == 0 && c->func->chunk.linesCount > 0) {
		line = c->func->chunk.lines[c->func->chunk.linesCount - 1];
	}
	return writeByte(&c->func->chunk, b, line);
}

static size_t emitShort(Compiler *c, uint16_t s, int line) {
	size_t i = emitBytecode(c, (uint8_t) (s >> 8), line);
	emitBytecode(c, (uint8_t) s, line);
	return i;
}

static void enterScope(Compiler *c) {
	c->depth++;
}

static void exitScope(Compiler *c) {
	c->depth--;
	while(c->localsCount > 0 &&
		c->locals[c->localsCount - 1].depth > c->depth) {
			c->localsCount--;
			emitBytecode(c, OP_POP, 0);
	}
}

static void discardScope(Compiler *c) {
	int depth = c->depth - 1;
	int localsCount = c->localsCount;
	while(localsCount > 0 &&
		c->locals[localsCount - 1].depth > depth) {
			localsCount--;
			emitBytecode(c, OP_POP, 0);
	}
}

static void startLoop(Compiler *c) {
	c->loopDepth++;
}

static void endLoop(Compiler *c) {
	c->loopDepth--;
}

static uint8_t createConst(Compiler *c, Value constant, int line) {
	int index = addConstant(&c->func->chunk, constant);
	if(index == -1) {
		error(c, line, "too many constants "
				"in function %s", c->func->name->data);
		return 0;
	}
	return (uint8_t) index;
}

static uint8_t identifierConst(Compiler *c, Identifier *id, int line) {
	ObjString *idStr = copyString(c->vm, id->name, id->length);
	return createConst(c, OBJ_VAL(idStr), line);
}

static int resolveVariable(Compiler *c, Identifier *id, int line) {
	for(int i = c->localsCount - 1; i >= 0; i--) {
		if(identifierEquals(&c->locals[i].id, id)) {
			if (c->locals[i].depth == -1) {
				error(c, line, "Cannot read local"
						" variable in its own initializer.");
				return 0;
			}
			return i;
		}
	}
	return -1;
}

static void addLocal(Compiler *c, Identifier *id, int line) {
	if(c->localsCount == MAX_LOCALS) {
		error(c, line, "Too many local variables"
				" in function %s.", c->func->name->data);
		return;
	}

	Local *local = &c->locals[c->localsCount];
	local->id = *id;
	local->depth = -1;
	c->localsCount++;
}

static void declareVar(Compiler *c, Identifier *id, int line) {
	if(c->depth == 0) return;

	for(int i = c->localsCount - 1; i >= 0; i--) {
		if(c->locals[i].depth != -1 && c->locals[i].depth < c->depth) break;
		if(identifierEquals(&c->locals[i].id, id)) {
			error(c, line, "Variable %.*s already"
					" declared.", id->length, id->name);
		}
	}

	addLocal(c, id, line);
}

static void defineVar(Compiler *c, Identifier *id, int line) {
	if(c->depth == 0) {
		uint8_t idConst = identifierConst(c, id, line);
		emitBytecode(c, OP_DEFINE_GLOBAL, line);
		emitBytecode(c, idConst, line);
	} else {
		c->locals[c->localsCount - 1].depth = c->depth;
	}
}

static size_t emitJumpTo(Compiler *c, int jmpOpcode, size_t target, int line) {
	int32_t offset = target - (c->func->chunk.count + 3);
	if(offset > INT16_MAX || offset < INT16_MIN) {
		error(c, line, "Too much code to jump over.");
	}

	emitBytecode(c, jmpOpcode, 0);
	emitShort(c, (uint16_t) offset, 0);
	return c->func->chunk.count - 2;
}

static void setJumpTo(Compiler *c, size_t jumpAddr, size_t target, int line) {
	int32_t offset = target - (jumpAddr + 3);

	if(offset > INT16_MAX || offset < INT16_MIN) {
		error(c, line, "Too much code to jump over.");
	}

	Chunk *chunk = &c->func->chunk;
	chunk->code[jumpAddr + 1] = (uint8_t) ((uint16_t) offset >> 8);
	chunk->code[jumpAddr + 2] = (uint8_t) ((uint16_t) offset);
}

static void compileExpr(Compiler *c, Expr *e);

static void compileBinaryExpr(Compiler *c, Expr *e) {
	compileExpr(c, e->bin.left);
	compileExpr(c, e->bin.right);
	switch(e->bin.op) {
	case PLUS:  emitBytecode(c, OP_ADD, e->line);  break;
	case MINUS: emitBytecode(c, OP_SUB, e->line);  break;
	case MULT:  emitBytecode(c, OP_MUL, e->line);  break;
	case DIV:   emitBytecode(c, OP_DIV, e->line);  break;
	case MOD:   emitBytecode(c, OP_MOD, e->line);  break;
	case EQ:    emitBytecode(c, OP_EQ, e->line);   break;
	case NEQ:   emitBytecode(c, OP_NEQ, e->line);  break;
	case GT:    emitBytecode(c, OP_GT, e->line);   break;
	case GE:    emitBytecode(c, OP_GE, e->line);   break;
	case LT:    emitBytecode(c, OP_LT, e->line);   break;
	case LE:    emitBytecode(c, OP_LE, e->line);   break;
	case IS:    emitBytecode(c, OP_IS, e->line);   break;
	default:
		error(c, e->line, "Wrong operator for binary expression.");
		break;
	}
}

static void compileLogicExpr(Compiler *c, Expr *e) {
	compileExpr(c, e->bin.left);
	emitBytecode(c, OP_DUP, e->line);

	uint8_t jmp = e->bin.op == AND ? OP_JUMPF : OP_JUMPT;
	size_t scJmp = emitBytecode(c, jmp, 0);
	emitShort(c, 0, 0);

	emitBytecode(c, OP_POP, e->line);
	compileExpr(c, e->bin.right);

	setJumpTo(c, scJmp, c->func->chunk.count, e->line);
}

static void compileUnaryExpr(Compiler *c, Expr *e) {
	compileExpr(c, e->unary.operand);
	switch(e->unary.op) {
	case MINUS: emitBytecode(c, OP_NEG, e->line); break;
	case NOT:   emitBytecode(c, OP_NOT, e->line); break;
	default:
		error(c, e->line, "Wrong operator for unary expression.");
		break;
	}
}

static void compileAssignExpr(Compiler *c, Expr *e) {
	compileExpr(c, e->assign.rval);

	switch(e->assign.lval->type) {
	case VAR_LIT: {
		int i = resolveVariable(c, &e->assign.lval->var.id, e->line);
		if(i != -1) {
			emitBytecode(c, OP_SET_LOCAL, e->line);
			emitBytecode(c, (uint8_t) i, e->line);
		} else {
			emitBytecode(c, OP_SET_GLOBAL, e->line);
			uint8_t id = identifierConst(c, &e->assign.lval->var.id, e->line);
			emitBytecode(c, id, e->line);
		}
		break;
	}
	case ACCESS_EXPR: {
		Expr *acc = e->assign.lval;

		compileExpr(c, acc->accessExpr.left);

		uint8_t id = identifierConst(c, &acc->accessExpr.id, e->line);
		emitBytecode(c, OP_SET_FIELD, e->line);
		emitBytecode(c, id, e->line);
		break;
	}
	case ARR_ACC: {
		Expr *acc = e->assign.lval;

		compileExpr(c, acc->arrAccExpr.left);
		compileExpr(c, acc->arrAccExpr.index);

		emitBytecode(c, OP_ARR_SET, e->line);
		break;
	}
	default: break;
	}
}

static void compileCallExpr(Compiler *c, Expr *e) {
	Opcode callCode   = OP_CALL;
	Opcode callInline = OP_CALL_0;

	Expr *callee = e->callExpr.callee;
	bool isMethod = callee->type == ACCESS_EXPR;

	if(isMethod) {
		bool isSuper = callee->accessExpr.left->type == SUPER_LIT;
		int line = callee->accessExpr.left->line;

		if(isSuper && c->type != TYPE_METHOD && c->type != TYPE_CTOR) {
			error(c, line, "Can't use `super` outside method.");
		}

		callCode   = isSuper ? OP_SUPER : OP_INVOKE;
		callInline = isSuper ? OP_SUPER_0 : OP_INVOKE_0;
		if(isSuper) {
			emitBytecode(c, OP_GET_LOCAL, e->line);
			emitBytecode(c, 0, e->line);
		} else {
			compileExpr(c, callee->accessExpr.left);
		}
	} else {
		compileExpr(c, callee);
	}

	LinkedList *n;
	uint8_t argsc = 0;
	foreach(n, e->callExpr.args->exprList.lst) {
		if(argsc == UINT8_MAX) {
			error(c, e->line,
				"Too many arguments for function %s.", c->func->name->data);
			return;
		}

		argsc++;
		compileExpr(c, (Expr*) n->elem);
	}

	if(argsc <= 10) {
		emitBytecode(c, callInline + argsc, e->line);
	} else {
		emitBytecode(c, callCode, e->line);
		emitBytecode(c, argsc, e->line);
	}

	if(isMethod) {
		uint8_t id = identifierConst(c, &callee->accessExpr.id, e->line);
		emitBytecode(c, id, e->line);
	}
}

static void compileAccessExpression(Compiler *c, Expr *e) {
	compileExpr(c, e->accessExpr.left);
	uint8_t id = identifierConst(c, &e->accessExpr.id, e->line);
	emitBytecode(c, OP_GET_FIELD, e->line);
	emitBytecode(c, id, e->line);
}

static void compileArraryAccExpression(Compiler *c, Expr *e) {
	compileExpr(c, e->arrAccExpr.left);
	compileExpr(c, e->arrAccExpr.index);
	emitBytecode(c, OP_ARR_GET, e->line);
}

static void compileVar(Compiler *c, Identifier *id, int line) {
	int i = resolveVariable(c, id, line);
	if(i != -1) {
		emitBytecode(c, OP_GET_LOCAL, line);
		emitBytecode(c, (uint8_t) i, line);
	} else {
		emitBytecode(c, OP_GET_GLOBAL, line);
		emitBytecode(c, identifierConst(c, id, line), line);
	}
}

static ObjString *readString(Compiler *c, Expr *e);

static void compileExpr(Compiler *c, Expr *e) {
	switch(e->type) {
	case ASSIGN:
	 	compileAssignExpr(c, e);
		break;
	case BINARY:
		if(e->bin.op == AND || e->bin.op == OR) {
			compileLogicExpr(c, e);
		} else {
			compileBinaryExpr(c, e);
		}
		break;
	case UNARY:
		compileUnaryExpr(c, e);
		break;
	case CALL_EXPR:
		compileCallExpr(c, e);
		break;
	case ACCESS_EXPR:
		compileAccessExpression(c, e);
		break;
	case ARR_ACC:
		compileArraryAccExpression(c, e);
		break;
	case EXPR_LST: {
		LinkedList *n;
		foreach(n, e->exprList.lst) {
			compileExpr(c, (Expr*) n->elem);
		}
		break;
	}
	case NUM_LIT:
		emitBytecode(c, OP_GET_CONST, e->line);
		emitBytecode(c, createConst(c, NUM_VAL(e->num), e->line), e->line);
		break;
	case BOOL_LIT:
		emitBytecode(c, OP_GET_CONST, e->line);
		emitBytecode(c, createConst(c, BOOL_VAL(e->boolean), e->line), e->line);
		break;
	case STR_LIT: {
		ObjString *str = readString(c, e);
		emitBytecode(c, OP_GET_CONST, e->line);
		emitBytecode(c, createConst(c, OBJ_VAL(str), e->line), e->line);
		break;
	}
	case VAR_LIT: {
		compileVar(c, &e->var.id, e->line);
		break;
	}
	case NULL_LIT:
		emitBytecode(c, OP_NULL, e->line);
		break;
	case ARR_LIT: {
		emitBytecode(c, OP_NEW_LIST, e->line);
		LinkedList *exprs = e->arr.exprs->exprList.lst;

		LinkedList *n;
		foreach(n, exprs) {
			compileExpr(c, (Expr*) n->elem);
			emitBytecode(c, OP_APPEND_LIST, e->line);
		}
		break;
	}
	case SUPER_LIT:
		error(c, e->line, "Can only use `super` inside methods");
		break;
	}
}

static void compileVarDecl(Compiler *c, Stmt *s) {
	declareVar(c, &s->varDecl.id, s->line);

	if(s->varDecl.init != NULL) {
		compileExpr(c, s->varDecl.init);
	} else {
		emitBytecode(c, OP_NULL, s->line);
	}

	defineVar(c, &s->varDecl.id, s->line);
}

static void compileStatement(Compiler *c, Stmt *s);
static void compileStatements(Compiler *c, LinkedList *stmts);

static void compileReturn(Compiler *c, Stmt *s) {
	if(c->prev == NULL) {
		error(c, s->line, "Cannot use return in global scope.");
	}
	if(c->type == TYPE_CTOR) {
		error(c, s->line, "Cannot use return in constructor.");
	}

	if(s->returnStmt.e != NULL) {
		compileExpr(c, s->returnStmt.e);
	} else {
		emitBytecode(c, OP_NULL, s->line);
	}

	emitBytecode(c, OP_RETURN, s->line);
}

static void compileIfStatement(Compiler *c, Stmt *s) {
	// compile the condition
	compileExpr(c, s->ifStmt.cond);

	// emit the jump istr for false condtion with dummy address
	size_t falseJmp = emitBytecode(c, OP_JUMPF, 0);
	emitShort(c, 0, 0);

	// compile 'then' branch
	compileStatement(c, s->ifStmt.thenStmt);

	// if the 'if' has an 'else' emit istruction to jump over the 'else' branch
	size_t exitJmp = 0;
	if(s->ifStmt.elseStmt != NULL) {
		exitJmp = emitBytecode(c, OP_JUMP, 0);
		emitShort(c, 0, 0);
	}

	// set the false jump to the 'else' branch (or to exit if not present)
	setJumpTo(c, falseJmp, c->func->chunk.count, s->line);

	// If present compile 'else' branch and set the exit jump to 'else' end
	if(s->ifStmt.elseStmt != NULL) {
		compileStatement(c, s->ifStmt.elseStmt);
		setJumpTo(c, exitJmp, c->func->chunk.count, s->line);
	}
}

static void patchLoopExitStmts(Compiler *c, size_t start, size_t cont, size_t brk) {
	for(size_t i = start; i < c->func->chunk.count; i++) {
		Opcode code = c->func->chunk.code[i];

		if(code == OP_SING_BRK || code == OP_SIGN_CONT) {
			c->func->chunk.code[i] = OP_JUMP;
			setJumpTo(c, i, code == OP_SIGN_CONT ? cont : brk, 0);
			code = OP_JUMP;
		}

		i += opcodeArgsNumber(code);
	}
}

static void compileForStatement(Compiler *c, Stmt *s) {
	enterScope(c);

	// init
	if(s->forStmt.init != NULL) {
		compileStatement(c, s->forStmt.init);
	}

	startLoop(c);

	// condition
	size_t forStart = c->func->chunk.count;
	size_t cont = forStart;

	size_t exitJmp = 0;
	if(s->forStmt.cond != NULL) {
		compileExpr(c, s->forStmt.cond);
		exitJmp = emitBytecode(c, OP_JUMPF, 0);
		emitShort(c, 0, 0);
	}

	// body
	compileStatement(c, s->forStmt.body);

	// act
	if(s->forStmt.act != NULL) {
		cont = c->func->chunk.count;
		compileExpr(c, s->forStmt.act);
		emitBytecode(c, OP_POP, 0);
	}

	// jump back to for start
	emitJumpTo(c, OP_JUMP, forStart, 0);
	endLoop(c);

	// set the exit jump
	if(s->forStmt.cond != NULL) {
		setJumpTo(c, exitJmp, c->func->chunk.count, s->line);
	}

	patchLoopExitStmts(c, forStart, cont, c->func->chunk.count);

	exitScope(c);
}

static void callMethod(Compiler *c, const char *name, int args) {
	Identifier method = {strlen(name), name};
	emitBytecode(c, OP_INVOKE_0 + args, 0);
	emitBytecode(c, identifierConst(c, &method, 0), 0);
}

/*
 * for(var i in iterable) {
 *     ...
 * }
 *
 * {
 *     var _iter = iterable.__iterator__()
 *     var i
 *     while(_iter.hasNext()) {
 *         i = _iter.next()
 *         ...
 *     }
 * }
**/
static void compileForEach(Compiler *c, Stmt *s) {
	enterScope(c);

	// set the iterator variable with a name that it's not an identifier.
	// this will avoid the user shadowing the iterator with a declared variable.
	Identifier iterator = {1, "."};
	declareVar(c, &iterator, s->line);
	defineVar(c, &iterator, s->line);

	int iteratorID = c->localsCount - 1;

	// declare the variable used for iteration
	declareVar(c, &s->forEach.var->varDecl.id, s->line);
	defineVar(c, &s->forEach.var->varDecl.id, s->line);
	int varID = c->localsCount - 1;

	// call the iterator() method over the object
	compileExpr(c, s->forEach.iterable);
	callMethod(c, "__iterator__", 0);

	emitBytecode(c, OP_NULL, 0);

	startLoop(c);
	size_t start = c->func->chunk.count;

	emitBytecode(c, OP_GET_LOCAL, 0);
	emitBytecode(c, iteratorID, 0);
	callMethod(c, "hasNext", 0);

	size_t exitJmp = emitBytecode(c, OP_JUMPF, 0);
	emitShort(c, 0, 0);

	emitBytecode(c, OP_GET_LOCAL, 0);
	emitBytecode(c, iteratorID, 0);
	callMethod(c, "next", 0);

	emitBytecode(c, OP_SET_LOCAL, 0);
	emitBytecode(c, varID, 0);
	emitBytecode(c, OP_POP, 0);

	compileStatement(c, s->forEach.body);

	emitJumpTo(c, OP_JUMP, start, 0);
	endLoop(c);

	setJumpTo(c, exitJmp, c->func->chunk.count, s->line);
	patchLoopExitStmts(c, start, start, c->func->chunk.count);

	exitScope(c);
}


static void compileWhileStatement(Compiler *c, Stmt *s) {
	startLoop(c);
	size_t start = c->func->chunk.count;

	compileExpr(c, s->whileStmt.cond);
	size_t exitJmp = emitBytecode(c, OP_JUMPF, 0);
	emitShort(c, 0, 0);

	compileStatement(c, s->whileStmt.body);

	emitJumpTo(c, OP_JUMP, start, 0);
	endLoop(c);

	setJumpTo(c, exitJmp, c->func->chunk.count, s->line);
	patchLoopExitStmts(c, start, start, c->func->chunk.count);
}

static void compileFunction(Compiler *c, Stmt *s) {
	Compiler funComp;
	initCompiler(&funComp, c, TYPE_FUNC, c->depth + 1, c->vm);

	ObjFunction *func = function(&funComp, c->func->module, s);

	uint8_t fnConst = createConst(c, OBJ_VAL(func), s->line);
	uint8_t idConst = identifierConst(c, &s->funcDecl.id, s->line);

	emitBytecode(c, OP_GET_CONST, s->line);
	emitBytecode(c, fnConst, s->line);
	emitBytecode(c, OP_DEFINE_GLOBAL, s->line);
	emitBytecode(c, idConst, s->line);

	endCompiler(&funComp);
}

static void compileNative(Compiler *c, Stmt *s) {
	size_t length = listLength(s->nativeDecl.formalArgs);
	ObjNative *native = newNative(c->vm, c->func->module, NULL, length, NULL);

	uint8_t n = createConst(c, OBJ_VAL(native), s->line);
	uint8_t i = identifierConst(c, &s->nativeDecl.id, s->line);
	native->name = AS_STRING(c->func->chunk.consts.arr[i]);

	emitBytecode(c, OP_GET_CONST, s->line);
	emitBytecode(c, n, s->line);

	emitBytecode(c, OP_DEFINE_NATIVE, s->line);
	emitBytecode(c, i, s->line);
}

static void compileMethods(Compiler *c, Stmt* cls) {
	LinkedList *methods = cls->classDecl.methods;
	LinkedList *n;

	Compiler methodc;
	foreach(n, methods) {
		Stmt *m = (Stmt*) n->elem;
		switch(m->type) {
		case FUNCDECL: {
			initCompiler(&methodc, c, TYPE_METHOD, c->depth + 1, c->vm);

			ObjFunction *met = method(&methodc, c->func->module, &cls->classDecl.id, m);

			emitBytecode(c, OP_DEF_METHOD, cls->line);
			emitBytecode(c, identifierConst(c, &m->funcDecl.id, m->line), cls->line);
			emitBytecode(c, createConst(c, OBJ_VAL(met), m->line), cls->line);

			endCompiler(&methodc);
			break;
		}
		case NATIVEDECL: {
			Identifier ctor = {strlen(CTOR_STR), CTOR_STR};
			if(identifierEquals(&ctor, &m->nativeDecl.id)) {
				error(c, m->line, "Cannot declare native constructor");
			}

			size_t argsLen = listLength(m->nativeDecl.formalArgs);
			ObjNative *n = newNative(c->vm, c->func->module, NULL, argsLen, NULL);

			uint8_t native = createConst(c, OBJ_VAL(n), cls->line);
			uint8_t id = identifierConst(c, &m->nativeDecl.id, m->line);

			Identifier *classId = &cls->classDecl.id;
			size_t len = classId->length + m->nativeDecl.id.length + 1;
			char *name = GC_ALLOC(c->vm, len + 1);

			memcpy(name, classId->name, classId->length);
			name[classId->length] = '.';
			memcpy(name + classId->length + 1, m->nativeDecl.id.name, m->nativeDecl.id.length);
			name[len] = '\0';

			n->name = newStringFromBuf(c->vm, name, len);

			emitBytecode(c, OP_NAT_METHOD, cls->line);
			emitBytecode(c, id, cls->line);
			emitBytecode(c, native, cls->line);
			break;
		}
		default: break;
		}
	}

}

static void compileClass(Compiler *c, Stmt *s) {
	uint8_t id = identifierConst(c, &s->classDecl.id, s->line);

	bool isSubClass = s->classDecl.sup != NULL;
	if(isSubClass) {
		compileExpr(c, s->classDecl.sup);
		emitBytecode(c, OP_NEW_SUBCLASS, s->line);
	} else {
		emitBytecode(c, OP_NEW_CLASS, s->line);
	}

	emitBytecode(c, id, s->line);

	compileMethods(c, s);

	declareVar(c, &s->classDecl.id, s->line);
	defineVar(c, &s->classDecl.id, s->line);
}

static void compileImportStatement(Compiler *c, Stmt *s) {
	bool isAs = s->importStmt.as.name != NULL;

	emitBytecode(c, isAs ? OP_IMPORT_AS : OP_IMPORT, s->line);
	uint8_t name = identifierConst(c, &s->importStmt.module, s->line);
	emitBytecode(c, name, s->line);

	if(isAs) {
		uint8_t as = identifierConst(c, &s->importStmt.as, s->line);
		emitBytecode(c, as, s->line);
	}

	emitBytecode(c, OP_POP, s->line);
}

static void compileExcept(Compiler *c, LinkedList *excs) {
	Stmt *exc = (Stmt*) excs->elem;

	emitBytecode(c, OP_DUP, exc->line);
	compileExpr(c, exc->excStmt.cls);
	emitBytecode(c, OP_IS, 0);

	size_t falseJmp = emitBytecode(c, OP_JUMPF, 0);
	emitShort(c, 0, 0);

	enterScope(c);

	emitBytecode(c, OP_EXC_HANDLED, 0);

	declareVar(c, &exc->excStmt.var, exc->line);
	defineVar(c, &exc->excStmt.var, exc->line);

	compileStatements(c, exc->excStmt.block->blockStmt.stmts);

	exitScope(c);

	size_t exitJmp = 0;
	if(excs->next != NULL) {
		exitJmp = emitBytecode(c, OP_JUMP, 0);
		emitShort(c, 0, 0);
	}

	setJumpTo(c, falseJmp, c->func->chunk.count, exc->line);

	if(excs->next != NULL) {
		compileExcept(c, excs->next);
		setJumpTo(c, exitJmp, c->func->chunk.count, exc->line);
	}
}

static void compileTryExcept(Compiler *c, Stmt *s) {
	if(c->tryDepth > MAX_TRY_DEPTH) {
		error(c, s->line, "Exceeded max number of nested try blocks (%d)", MAX_TRY_DEPTH);
	}

	uint8_t setup = emitBytecode(c, OP_SETUP_TRY, s->line);
	emitShort(c, 0, 0);

	compileStatement(c, s->tryStmt.block);

	emitBytecode(c, OP_END_TRY, 0);
	uint8_t excJmp = emitBytecode(c, OP_JUMP, 0);
	emitShort(c, 0, 0);

	setJumpTo(c, setup, c->func->chunk.count, s->line);

	compileExcept(c, s->tryStmt.excs);

	emitBytecode(c, OP_EXC_HANDLER_END, 0);

	setJumpTo(c, excJmp, c->func->chunk.count, 0);
}

static void compileRaiseStmt(Compiler *c, Stmt *s) {
	compileExpr(c, s->raiseStmt.exc);
	emitBytecode(c, OP_RAISE, s->line);
}

static void compileStatement(Compiler *c, Stmt *s) {
	switch(s->type) {
	case IF:
		compileIfStatement(c, s);
		break;
	case FOR:
		compileForStatement(c, s);
		break;
	case FOREACH:
		compileForEach(c, s);
		break;
	case WHILE:
		compileWhileStatement(c, s);
		break;
	case BLOCK:
		enterScope(c);
		compileStatements(c, s->blockStmt.stmts);
		exitScope(c);
		break;
	case RETURN_STMT:
		compileReturn(c, s);
		break;
	case EXPR:
		compileExpr(c, s->exprStmt);
		emitBytecode(c, OP_POP, 0);
		break;
	case VARDECL:
		compileVarDecl(c, s);
		break;
	case FUNCDECL:
		compileFunction(c, s);
		break;
	case NATIVEDECL:
		compileNative(c, s);
		break;
	case CLASSDECL:
		compileClass(c, s);
		break;
	case IMPORT:
		compileImportStatement(c, s);
		break;
	case RAISE_STMT:
		compileRaiseStmt(c, s);
		break;
	case TRY_STMT:
		c->tryDepth++;
		compileTryExcept(c, s);
		c->tryDepth--;
		break;
	case CONTINUE_STMT:
		if(c->loopDepth == 0) {
			error(c, s->line, "cannot use continue outside loop.");
			break;
		}
		discardScope(c);
		emitBytecode(c, OP_SIGN_CONT, s->line);
		emitShort(c, 0, 0);
		break;
	case BREAK_STMT:
		if(c->loopDepth == 0) {
			error(c, s->line, "cannot use break outside loop.");
			break;
		}
		discardScope(c);
		emitBytecode(c, OP_SING_BRK, s->line);
		emitShort(c, 0, 0);
		break;
	case EXCEPT_STMT: break;
	}
}

static void compileStatements(Compiler *c, LinkedList *stmts) {
	LinkedList *n;
	foreach(n, stmts) {
		compileStatement(c, (Stmt*) n->elem);
	}
}

ObjFunction *compile(BlangVM *vm, ObjModule *module, Stmt *s) {
	Compiler c;

	initCompiler(&c, NULL, TYPE_FUNC, -1, vm);
	ObjFunction *func = function(&c, module, s);
	endCompiler(&c);

	return c.hadError ? NULL : func;
}

static ObjFunction *function(Compiler *c, ObjModule *module, Stmt *s) {
	c->func = newFunction(c->vm, module, NULL, listLength(s->funcDecl.formalArgs));
	if(s->funcDecl.id.length != 0) {
		c->func->name = copyString(c->vm,
			s->funcDecl.id.name, s->funcDecl.id.length);
	}

	enterScope(c);

	//add phony variable for function receiver (in the case of functions the
	//receiver is the function itself but it ins't accessible)
	Identifier id = {0, ""};
	addLocal(c, &id, 0);
	c->locals[c->localsCount - 1].depth = c->depth;

	LinkedList *n;
	foreach(n, s->funcDecl.formalArgs) {
		declareVar(c, (Identifier*) n->elem, s->line);
		defineVar(c, (Identifier*) n->elem, s->line);
	}

	compileStatements(c, s->funcDecl.body->blockStmt.stmts);

	emitBytecode(c, OP_NULL, 0);
	emitBytecode(c, OP_RETURN, 0);

	exitScope(c);

	return c->func;
}

static ObjFunction *method(Compiler *c, ObjModule *module, Identifier *classId, Stmt *s) {
	c->func = newFunction(c->vm, module, NULL, listLength(s->funcDecl.formalArgs));

	//create new method name by concatenating the class name to it
	size_t length = classId->length + s->funcDecl.id.length + 1;
	char *name = GC_ALLOC(c->vm, length + 1);

	memcpy(name, classId->name, classId->length);
	name[classId->length] = '.';
	memcpy(name + classId->length + 1, s->funcDecl.id.name, s->funcDecl.id.length);
	name[length] = '\0';

	c->func->name = newStringFromBuf(c->vm, name, length);

	//if in costructor change the type
	Identifier ctor = {strlen(CTOR_STR), CTOR_STR};
	if(identifierEquals(&s->funcDecl.id, &ctor)) {
		c->type = TYPE_CTOR;
	}

	enterScope(c);

	//add `this` for method receiver (the object from which was called)
	Identifier thisId = {strlen(THIS_STR), THIS_STR};
	addLocal(c, &thisId, s->line);
	c->locals[c->localsCount - 1].depth = c->depth;

	//define and declare arguments
	LinkedList *n;
	foreach(n, s->funcDecl.formalArgs) {
		declareVar(c, (Identifier*) n->elem, s->line);
		defineVar(c, (Identifier*) n->elem, s->line);
	}

	compileStatements(c, s->funcDecl.body->blockStmt.stmts);

	//if in constructor return the instance
	if(c->type == TYPE_CTOR) {
		emitBytecode(c, OP_GET_LOCAL, 0);
		emitBytecode(c, 0, 0);
	} else {
		emitBytecode(c, OP_NULL, 0);
	}
	emitBytecode(c, OP_RETURN, 0);

	exitScope(c);

	return c->func;
}

static ObjString *readString(Compiler *c, Expr *e) {
	const char *str = e->str.str + 1;
	size_t length = e->str.length - 2;

	StringBuffer sb;
	sbuf_create(&sb);
	for(size_t i = 0; i < length; i++) {
		char c = str[i];
		if (c == '\\') {
			switch(str[i + 1]) {
			case '0':  sbuf_appendchar(&sb, '\0'); break;
			case 'a':  sbuf_appendchar(&sb, '\a'); break;
			case 'b':  sbuf_appendchar(&sb, '\b'); break;
			case 'f':  sbuf_appendchar(&sb, '\f'); break;
			case 'n':  sbuf_appendchar(&sb, '\n'); break;
			case 'r':  sbuf_appendchar(&sb, '\r'); break;
			case 't':  sbuf_appendchar(&sb, '\t'); break;
			case 'v':  sbuf_appendchar(&sb, '\v'); break;
			default:   sbuf_appendchar(&sb, str[i + 1]); break;
			}
			i++;
		} else {
		  sbuf_appendchar(&sb, c);
	  	}
	}

	ObjString *s = copyString(c->vm, sbuf_get_backing_buf(&sb),  sbuf_get_len(&sb));
	sbuf_destroy(&sb);

	return s;
}

void reachCompilerRoots(BlangVM *vm, Compiler *c) {
	while(c != NULL) {
		reachObject(vm, (Obj*) c->func);
		c = c->prev;
	}
}
