#include "compiler.h"
#include "memory.h"
#include "opcode.h"
#include "value.h"
#include "util.h"
#include "vm.h"

#include "util/stringbuf.h"

#include "parse/ast.h"

#include <stdio.h>
#include <string.h>
#include <stdarg.h>

#define MAX_TRY_DEPTH HANDLER_MAX

typedef struct Local {
	Identifier id;
	bool isUpvalue;
	int depth;
} Local;

typedef struct Upvalue {
	bool isLocal;
	uint8_t index;
} Upvalue;

typedef struct Loop {
	int depth;
	struct Loop *next;
} Loop;

typedef enum FuncType {
	TYPE_FUNC, TYPE_METHOD, TYPE_CTOR
} FuncType;

typedef struct Compiler {
	BlangVM *vm;
	Compiler *prev;

	bool hasSuper;

	Loop *loops;

	FuncType type;
	ObjFunction *func;

	uint8_t localsCount;
	Local locals[MAX_LOCALS];

	Upvalue upvalues[MAX_LOCALS];

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
	c->loops = NULL;
	c->tryDepth = 0;
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

static void discardLocal(Compiler *c, Local *local) {
	if(local->isUpvalue) {
		emitBytecode(c, OP_CLOSE_UPVALUE, 0);
	} else {
		emitBytecode(c, OP_POP, 0);
	}
}

static void enterScope(Compiler *c) {
	c->depth++;
}

static void exitScope(Compiler *c) {
	c->depth--;
	while(c->localsCount > 0 && 
		c->locals[c->localsCount - 1].depth > c->depth) {
			discardLocal(c, &c->locals[--c->localsCount]);
	}
}

static void discardScope(Compiler *c, int depth) {
	int localsCount = c->localsCount;
	while(localsCount > 0 &&
		c->locals[localsCount - 1].depth > depth) {
			discardLocal(c, &c->locals[--localsCount]);
	}
}

static void startLoop(Compiler *c, Loop *loop) {
	loop->depth = c->depth;
	loop->next = c->loops;
	c->loops = loop;
}

static void endLoop(Compiler *c) {
	c->loops = c->loops->next;
}

static uint16_t createConst(Compiler *c, Value constant, int line) {
	int index = addConstant(&c->func->chunk, constant);
	if(index == -1) {
		const char *name = c->func->name == NULL ? "<main>" : c->func->name->data;
		error(c, line, "too many constants in function %s", name);
		return 0;
	}
	return (uint16_t) index;
}

static uint16_t identifierConst(Compiler *c, Identifier *id, int line) {
	ObjString *idStr = copyString(c->vm, id->name, id->length, true);
	return createConst(c, OBJ_VAL(idStr), line);
}

static void addLocal(Compiler *c, Identifier *id, int line) {
	if(c->localsCount == MAX_LOCALS) {
		error(c, line, "Too many local variables"
				" in function %s.", c->func->name->data);
		return;
	}

	Local *local = &c->locals[c->localsCount];
	local->isUpvalue = false;
	local->depth = -1;
	local->id = *id;
	c->localsCount++;
}

static int resolveVariable(Compiler *c, Identifier *id, bool inFunc, int line) {
	for(int i = c->localsCount - 1; i >= 0; i--) {
		if(identifierEquals(&c->locals[i].id, id)) {
			if (inFunc && c->locals[i].depth == -1) {
				error(c, line, "Cannot read local"
						" variable in its own initializer.");
				return 0;
			}
			return i;
		}
	}
	return -1;
}

static int addUpvalue(Compiler *c, uint8_t index, bool local, int line) {
	uint8_t upvalueCount = c->func->upvaluec;
	for(uint8_t i = 0; i < upvalueCount; i++) {
		Upvalue *upval = &c->upvalues[i];
		if(upval->index == index && upval->isLocal == local) {
			return i;
		}
	}

	if(c->func->upvaluec == MAX_LOCALS) {
		error(c, line, "Too many upvalues in function %s.", c->func->name->data);
		return -1;
	}

	c->upvalues[c->func->upvaluec].isLocal = local;
	c->upvalues[c->func->upvaluec].index = index;
	return c->func->upvaluec++;
}

static int resolveUpvalue(Compiler *c, Identifier *id, int line) {
	if(c->prev == NULL) {
		return -1;
	}

	int i = resolveVariable(c->prev, id, false, line);
	if(i != -1) {
		c->prev->locals[i].isUpvalue = true;
		return addUpvalue(c, i, true, line);
	}
	
	i = resolveUpvalue(c->prev, id, line);
	if(i != -1) {
		return addUpvalue(c, i, false, line);
	}

	return -1;
}

static void declareVar(Compiler *c, Identifier *id, int line) {
	if(c->depth == 0) return;

	for(int i = c->localsCount - 1; i >= 0; i--) {
		if(c->locals[i].depth != -1 && c->locals[i].depth < c->depth) break;
		if(identifierEquals(&c->locals[i].id, id)) {
			error(c, line, "Variable `%.*s` already"
					" declared.", id->length, id->name);
		}
	}

	addLocal(c, id, line);
}

static void defineVar(Compiler *c, Identifier *id, int line) {
	if(c->depth == 0) {
		emitBytecode(c, OP_DEFINE_GLOBAL, line);
		emitShort(c, identifierConst(c, id, line), line);
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

static ObjString *readString(Compiler *c, Expr *e);

static void addDefaultConsts(Compiler *c, Value *defaults, LinkedList *defArgs) {
	int i = 0;

	LinkedList *n;
	foreach(n, defArgs) {
		Expr *e = (Expr*) n->elem;
		switch(e->type) {
		case NUM_LIT:
			defaults[i++] = NUM_VAL(e->num);
			break;
		case BOOL_LIT:
			defaults[i++] = BOOL_VAL(e->boolean);
			break;
		case STR_LIT:
			defaults[i++] = OBJ_VAL(readString(c, e));
			break;
		case NULL_LIT:
			defaults[i++] = NULL_VAL;
			break;
		default: break;
		}
	}
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
	case GT:    emitBytecode(c, OP_GT, e->line);   break;
	case GE:    emitBytecode(c, OP_GE, e->line);   break;
	case LT:    emitBytecode(c, OP_LT, e->line);   break;
	case LE:    emitBytecode(c, OP_LE, e->line);   break;
	case IS:    emitBytecode(c, OP_IS, e->line);   break;
	case NEQ:
		emitBytecode(c, OP_EQ, e->line);
		emitBytecode(c, OP_NOT, e->line);
		break;
	default:
		UNREACHABLE();
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
		UNREACHABLE();
		break;
	}
}

static void compileTernaryExpr(Compiler *c, Expr *e) {
	compileExpr(c, e->ternary.cond);

	size_t falseJmp = emitBytecode(c, OP_JUMPF, e->line);
	emitShort(c, 0, 0);

	compileExpr(c, e->ternary.thenExpr);
	size_t exitJmp = emitBytecode(c, OP_JUMP, e->line);
	emitShort(c, 0, 0);

	setJumpTo(c, falseJmp, c->func->chunk.count, e->line);
	compileExpr(c, e->ternary.elseExpr);

	setJumpTo(c, exitJmp, c->func->chunk.count, e->line);
}

static void compileVariable(Compiler *c, Identifier *id, bool set, int line) {
	int i = resolveVariable(c, id, true, line);
	if(i != -1) {
		if(set) emitBytecode(c, OP_SET_LOCAL, line);
		else    emitBytecode(c, OP_GET_LOCAL, line);
		emitBytecode(c, i, line);
	} else if((i = resolveUpvalue(c, id, line)) != -1){
		if(set) emitBytecode(c, OP_SET_UPVALUE, line);
		else    emitBytecode(c, OP_GET_UPVALUE, line);
		emitBytecode(c, i, line);
	} else {
		if(set) emitBytecode(c, OP_SET_GLOBAL, line);
		else    emitBytecode(c, OP_GET_GLOBAL, line);
		emitShort(c, identifierConst(c, id, line), line);
	}
}

static void compileLval(Compiler *c, Expr *e) {
	switch(e->type) {
	case VAR_LIT:
		compileVariable(c, &e->var.id, true, e->line);
		break;
	case ACCESS_EXPR: {
		compileExpr(c, e->accessExpr.left);

		emitBytecode(c, OP_SET_FIELD, e->line);
		emitShort(c, identifierConst(c, &e->accessExpr.id, e->line), e->line);
		break;
	}
	case ARR_ACC: {
		compileExpr(c, e->arrAccExpr.left);
		compileExpr(c, e->arrAccExpr.index);
		emitBytecode(c, OP_ARR_SET, e->line);
		break;
	}
	default:
		UNREACHABLE();
		break;
	}
	
}

static void compileAssignExpr(Compiler *c, Expr *e) {
	switch(e->assign.lval->type) {
	case VAR_LIT: {
		compileExpr(c, e->assign.rval);
		compileLval(c, e->assign.lval);
		break;
	}
	case ACCESS_EXPR: {
		compileExpr(c, e->assign.rval);
		compileLval(c, e->assign.lval);
		break;
	}
	case ARR_ACC: {
		compileExpr(c, e->assign.rval);
		compileLval(c, e->assign.lval);
		break;
	}
	case TUPLE_LIT: {
		compileExpr(c, e->assign.rval);
		emitBytecode(c, OP_UNPACK, e->line);

		int i = 0;
		Expr *ass[UINT8_MAX];
		
		LinkedList *n;
		foreach(n, e->assign.lval->tuple.exprs->exprList.lst) {
			if(i == UINT8_MAX) {
				error(c, e->line, "Exceeded max number of unpack assignment (%d).", UINT8_MAX);
				break;
			}
			ass[i++] = (Expr*) n->elem;
		}

		emitBytecode(c, (uint8_t) i, e->line);

		for(int n = i - 1; n >= 0; n--) {
			Expr *e = ass[n];
			compileLval(c, e);
			if(n != 0) emitBytecode(c, OP_POP, e->line);
		}
		break;
	}
	default: 
		UNREACHABLE(); 
		break;
	}
}

static void compileCompundAssign(Compiler *c, Expr *e) {
	Operator op = e->compundAssign.op;
	Expr *l = e->compundAssign.lval;
	Expr *r = e->compundAssign.rval;

	// expand compound assignement (e.g. a += b -> a = a + b)
	Expr binary = {e->line, BINARY, .bin = {op, l, r}};
	Expr assignment = {e->line, ASSIGN, .assign = {l, &binary}};

	// compile as a normal assignment
	compileAssignExpr(c, &assignment);
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
		emitShort(c, identifierConst(c, &callee->accessExpr.id, e->line), e->line);
	}
}

static void compileAccessExpression(Compiler *c, Expr *e) {
	compileExpr(c, e->accessExpr.left);
	emitBytecode(c, OP_GET_FIELD, e->line);
	emitShort(c, identifierConst(c, &e->accessExpr.id, e->line), e->line);
}

static void compileArraryAccExpression(Compiler *c, Expr *e) {
	compileExpr(c, e->arrAccExpr.left);
	compileExpr(c, e->arrAccExpr.index);
	emitBytecode(c, OP_ARR_GET, e->line);
}

static void compileExpExpr(Compiler *c, Expr *e) {
	compileExpr(c, e->expExpr.base);
	compileExpr(c, e->expExpr.exp);
	emitBytecode(c, OP_POW, e->line);
}

static void compileFunction(Compiler *c, Stmt *s);

static void compileAnonymousFunc(Compiler *c, Expr *e) {
	Stmt *f = e->anonFunc.func;

	char name[5 + MAX_STRLEN_FOR_INT_TYPE(int) + 1];
	sprintf(name, "anon:%d", f->line);
	
	f->funcDecl.id.length = strlen(name);
	f->funcDecl.id.name = name;

	compileFunction(c, f);
}

static ObjString *readString(Compiler *c, Expr *e);

static void compileExpr(Compiler *c, Expr *e) {
	switch(e->type) {
	case ASSIGN:
	 	compileAssignExpr(c, e);
		break;
	case COMP_ASSIGN:
		compileCompundAssign(c, e);
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
	case TERNARY:
		compileTernaryExpr(c, e);
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
	case EXP_EXPR:
		compileExpExpr(c, e);
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
		emitShort(c, createConst(c, NUM_VAL(e->num), e->line), e->line);
		break;
	case BOOL_LIT:
		emitBytecode(c, OP_GET_CONST, e->line);
		emitShort(c, createConst(c, BOOL_VAL(e->boolean), e->line), e->line);
		break;
	case STR_LIT: {
		ObjString *str = readString(c, e);
		emitBytecode(c, OP_GET_CONST, e->line);
		emitShort(c, createConst(c, OBJ_VAL(str), e->line), e->line);
		break;
	}
	case VAR_LIT: {
		compileVariable(c, &e->var.id, false, e->line);
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
	case TUPLE_LIT: {
		LinkedList *exprs = e->arr.exprs->exprList.lst;

		int i = 0;
		LinkedList *n;
		foreach(n, exprs) {
			compileExpr(c, (Expr*) n->elem);
			i++;
		}

		if(i >= UINT8_MAX) {
			error(c, e->line, "too many elements in tuple");
			break;
		}

		emitBytecode(c, OP_NEW_TUPLE, e->line);
		emitBytecode(c, i, e->line);
		break;
	}
	case ANON_FUNC:
		compileAnonymousFunc(c, e);
		break;
	case SUPER_LIT:
		error(c, e->line, "Can only use `super` inside methods");
		break;
	}
}

static void compileVarDecl(Compiler *c, Stmt *s) {
	LinkedList *n;

	int num = 0;
	Identifier *ids[UINT8_MAX + 1];

	foreach(n, s->varDecl.ids) {
		ids[num++] = (Identifier*) n->elem;
	}

	for(int i = 0; i < num; i++) {
		declareVar(c, ids[i], s->line);
	}

	if(s->varDecl.init != NULL) {
		compileExpr(c, s->varDecl.init);

		if(s->varDecl.isUnpack) {
			emitBytecode(c, OP_UNPACK, s->line);
			emitBytecode(c, (uint8_t) num, s->line);
		}
	} else {
		for(int i = 0; i < num; i++) {
			emitBytecode(c, OP_NULL, s->line);
		}
	}

	// define in reverse ordeer in order to define global vars
	// in the right order
	for(int i = num - 1; i >= 0; i--) {
		if(c->depth == 0)
			defineVar(c, ids[i], s->line);
		else
			c->locals[c->localsCount - i - 1].depth = c->depth;
	}
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

		if(code == OP_SIGN_BRK || code == OP_SIGN_CONT) {
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

	Loop l;
	startLoop(c, &l);

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
	emitShort(c, identifierConst(c, &method, 0), 0);
}

/*
 * for(var i in iterable) {
 *     ...
 * }
 *
 * {
 *     var _iter = iterable.__iterator__()
 *     while(_iter.hasNext()) {
 *         var i = _iter.next()
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

	// call the iterator() method over the object
	compileExpr(c, s->forEach.iterable);
	callMethod(c, "__iterator__", 0);

	Loop l;
	startLoop(c, &l);

	size_t start = c->func->chunk.count;

	emitBytecode(c, OP_GET_LOCAL, 0);
	emitBytecode(c, iteratorID, 0);
	callMethod(c, "hasNext", 0);

	size_t exitJmp = emitBytecode(c, OP_JUMPF, 0);
	emitShort(c, 0, 0);

	emitBytecode(c, OP_GET_LOCAL, 0);
	emitBytecode(c, iteratorID, 0);
	callMethod(c, "next", 0);

	Stmt *varDecl = s->forEach.var;

	enterScope(c);

	// declare the variables used for iteration
	int num = 0;
	LinkedList *n;
	foreach(n, varDecl->varDecl.ids) {
		declareVar(c, (Identifier*) n->elem, s->line);
		defineVar(c, (Identifier*) n->elem, s->line);
		num++;
	}

	if(varDecl->varDecl.isUnpack) {
		emitBytecode(c, OP_UNPACK, s->line);
		emitBytecode(c, (uint8_t) num, s->line);
	}

	compileStatements(c, s->forEach.body->blockStmt.stmts);

	exitScope(c);

	emitJumpTo(c, OP_JUMP, start, 0);
	endLoop(c);

	setJumpTo(c, exitJmp, c->func->chunk.count, s->line);
	patchLoopExitStmts(c, start, start, c->func->chunk.count);

	exitScope(c);
}


static void compileWhileStatement(Compiler *c, Stmt *s) {
	Loop l;
	startLoop(c, &l);

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
	Compiler compiler;
	initCompiler(&compiler, c, TYPE_FUNC, c->depth + 1, c->vm);

	ObjFunction *func = function(&compiler, c->func->module, s);

	emitBytecode(c, OP_NEW_CLOSURE, s->line);
	emitShort(c, createConst(c, OBJ_VAL(func), s->line), s->line);

	for(uint8_t i = 0; i < func->upvaluec; i++) {
		emitBytecode(c, compiler.upvalues[i].isLocal ? 1 : 0, s->line);
		emitBytecode(c, compiler.upvalues[i].index, s->line);
	}

	endCompiler(&compiler);
}

static void compileNative(Compiler *c, Stmt *s) {
	size_t defaults = listLength(s->nativeDecl.defArgs);
	size_t arity    = listLength(s->nativeDecl.formalArgs);

	ObjNative *native = newNative(c->vm, c->func->module, NULL, arity, NULL, defaults);
	addDefaultConsts(c, native->defaults, s->nativeDecl.defArgs);

	uint16_t n = createConst(c, OBJ_VAL(native), s->line);
	uint16_t i = identifierConst(c, &s->nativeDecl.id, s->line);
	native->name = AS_STRING(c->func->chunk.consts.arr[i]);

	emitBytecode(c, OP_GET_CONST, s->line);
	emitShort(c, n, s->line);

	emitBytecode(c, OP_DEFINE_NATIVE, s->line);
	emitShort(c, i, s->line);
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

			emitBytecode(c, OP_NEW_CLOSURE, m->line);
			emitShort(c, createConst(c, OBJ_VAL(met), m->line), m->line);

			for(uint8_t i = 0; i < met->upvaluec; i++) {
				emitBytecode(c, methodc.upvalues[i].isLocal ? 1 : 0, m->line);
				emitBytecode(c, methodc.upvalues[i].index, m->line);
			}

			emitBytecode(c, OP_DEF_METHOD, cls->line);
			emitShort(c, identifierConst(c, &m->funcDecl.id, m->line), cls->line);

			endCompiler(&methodc);
			break;
		}
		case NATIVEDECL: {
			Identifier ctor = {strlen(CTOR_STR), CTOR_STR};
			if(identifierEquals(&ctor, &m->nativeDecl.id)) {
				error(c, m->line, "Cannot declare native constructor");
			}

			size_t defaults = listLength(m->nativeDecl.defArgs);
			size_t arity = listLength(m->nativeDecl.formalArgs);

			ObjNative *n = newNative(c->vm, c->func->module, NULL, arity, NULL, defaults);
			addDefaultConsts(c, n->defaults, m->nativeDecl.defArgs);

			uint16_t native = createConst(c, OBJ_VAL(n), cls->line);
			uint16_t id = identifierConst(c, &m->nativeDecl.id, m->line);

			Identifier *classId = &cls->classDecl.id;
			size_t len = classId->length + m->nativeDecl.id.length + 1;
			ObjString *name = allocateString(c->vm, len);

			memcpy(name->data, classId->name, classId->length);
			name->data[classId->length] = '.';
			memcpy(name->data + classId->length + 1, m->nativeDecl.id.name, m->nativeDecl.id.length);

			n->name = name;

			emitBytecode(c, OP_NAT_METHOD, cls->line);
			emitShort(c, id, cls->line);
			emitShort(c, native, cls->line);
			break;
		}
		default: break;
		}
	}

}

static void compileClass(Compiler *c, Stmt *s) {
	bool isSubClass = s->classDecl.sup != NULL;
	if(isSubClass) {
		compileExpr(c, s->classDecl.sup);
		emitBytecode(c, OP_NEW_SUBCLASS, s->line);
	} else {
		emitBytecode(c, OP_NEW_CLASS, s->line);
	}

	emitShort(c, identifierConst(c, &s->classDecl.id, s->line), s->line);

	compileMethods(c, s);

	declareVar(c, &s->classDecl.id, s->line);
	defineVar(c, &s->classDecl.id, s->line);
}

static void compileImportStatement(Compiler *c, Stmt *s) {
	const char *base = ((Identifier*)s->importStmt.modules->elem)->name;
	LinkedList *n;

	uint16_t nameConst;

	// import module (if nested module, import all from outer to inner)
	size_t length = -1;
	foreach(n, s->importStmt.modules) {
		Identifier *name = (Identifier*) n->elem;

		// create fully qualified name of module
		length += name->length + 1;         // length of current submodule plus a dot
		Identifier module = {length, base}; // name of current submodule

		if(n == s->importStmt.modules && s->importStmt.
				impNames == NULL && s->importStmt.as.name == NULL) {
			emitBytecode(c, OP_IMPORT, s->line);
		} else {
			emitBytecode(c, OP_IMPORT_FROM, s->line);
		}
		nameConst = identifierConst(c, &module, s->line);
		emitShort(c, nameConst, s->line);

		if(n->next != NULL) {
			emitBytecode(c, OP_POP, s->line);
		}
	}

	if(s->importStmt.impNames != NULL) {
		foreach(n, s->importStmt.impNames) {
			emitBytecode(c, OP_IMPORT_NAME, s->line);
			emitShort(c, nameConst, s->line);
			emitShort(c, identifierConst(c, (Identifier*) n->elem, s->line), s->line);
		}
	} else if(s->importStmt.as.name != NULL) {
		// set last import as an import as
		c->func->chunk.code[c->func->chunk.count - 2] = OP_IMPORT_AS;
		// emit the as name
		emitShort(c, identifierConst(c, &s->importStmt.as, s->line), s->line);
	}

	emitBytecode(c, OP_POP, s->line);
}

static void compileExcepts(Compiler *c, LinkedList *excs) {
	Stmt *exc = (Stmt*) excs->elem;

	emitBytecode(c, OP_DUP, exc->line);
	compileExpr(c, exc->excStmt.cls);
	emitBytecode(c, OP_IS, 0);

	size_t falseJmp = emitBytecode(c, OP_JUMPF, 0);
	emitShort(c, 0, 0);

	enterScope(c);

	emitBytecode(c, OP_DUP, exc->line);

	declareVar(c, &exc->excStmt.var, exc->line);
	defineVar(c, &exc->excStmt.var, exc->line);

	compileStatements(c, exc->excStmt.block->blockStmt.stmts);

	emitBytecode(c, OP_NULL, exc->line);
	emitBytecode(c, OP_SET_LOCAL, exc->line);

	Identifier excId = {10, ".exception"};
	emitBytecode(c, resolveVariable(c, &excId, true, exc->line), exc->line);
	emitBytecode(c, OP_POP, exc->line);

	exitScope(c);

	size_t exitJmp = 0;
	if(excs->next != NULL) {
		exitJmp = emitBytecode(c, OP_JUMP, 0);
		emitShort(c, 0, 0);
	}

	setJumpTo(c, falseJmp, c->func->chunk.count, exc->line);

	if(excs->next != NULL) {
		compileExcepts(c, excs->next);
		setJumpTo(c, exitJmp, c->func->chunk.count, exc->line);
	}
}

static void enterTryBlock(Compiler *c, Stmt *try) {
	if(try->tryStmt.ensure != NULL && try->tryStmt.excs != NULL)
		c->tryDepth++;
	c->tryDepth++;
}

static void exitTryBlock(Compiler *c, Stmt *try) {
	if(try->tryStmt.ensure != NULL && try->tryStmt.excs != NULL)
		c->tryDepth--;
	c->tryDepth--;
}

static void compileTryExcept(Compiler *c, Stmt *s) {
	enterTryBlock(c, s);

	if(c->tryDepth > MAX_TRY_DEPTH) {
		error(c, s->line, "Exceeded max number of nested try blocks (%d)", MAX_TRY_DEPTH);
	}

	bool hasExcept = s->tryStmt.excs != NULL;
	bool hasEnsure = s->tryStmt.ensure != NULL;

	size_t excSetup = 0;
	size_t ensSetup = 0;

	if(hasEnsure) {
		ensSetup = emitBytecode(c, OP_SETUP_ENSURE, s->line);
		emitShort(c, 0, 0);
	}
	if(hasExcept) {
		excSetup = emitBytecode(c, OP_SETUP_EXCEPT, s->line);
		emitShort(c, 0, 0);
	}

	compileStatement(c, s->tryStmt.block);

	if(hasExcept)
		emitBytecode(c, OP_POP_HANDLER, s->line);
	
	if(hasEnsure) {
		emitBytecode(c, OP_POP_HANDLER, s->line);
		// esnure block expects exception on top or the
		// stack or null if no exception has been raised
		emitBytecode(c, OP_NULL, s->line);
		// the cause of the unwind null, CAUSE_RETURN or CAUSE_EXCEPT
		emitBytecode(c, OP_NULL, s->line);
	}

	size_t excJmp = 0;

	enterScope(c);

	Identifier cause = {6, ".cause"};
	declareVar(c, &cause, 0);
	defineVar(c, &cause, 0);

	Identifier exc = {10, ".exception"};
	declareVar(c, &exc, 0);
	defineVar(c, &exc, 0);

	if(hasExcept) {
		excJmp = emitBytecode(c, OP_JUMP, 0);
		emitShort(c, 0, 0);

		setJumpTo(c, excSetup, c->func->chunk.count, s->line);

		compileExcepts(c, s->tryStmt.excs);

		if(hasEnsure) {
			emitBytecode(c, OP_POP_HANDLER, 0);
		} else {
			emitBytecode(c, OP_ENSURE_END, 0);
			exitScope(c);
		}

		setJumpTo(c, excJmp, c->func->chunk.count, 0);
	}

	if(hasEnsure) {
		setJumpTo(c, ensSetup, c->func->chunk.count, s->line);
		compileStatements(c, s->tryStmt.ensure->blockStmt.stmts);
		emitBytecode(c, OP_ENSURE_END, 0);
		exitScope(c);
	}

	exitTryBlock(c, s);
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
		declareVar(c, &s->funcDecl.id, s->line);
		compileFunction(c, s);
		defineVar(c, &s->funcDecl.id, s->line);
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
		compileTryExcept(c, s);
		break;
	case CONTINUE_STMT:
		if(c->loops == NULL) {
			error(c, s->line, "cannot use continue outside loop.");
			break;
		}
		if(c->tryDepth != 0) {
			error(c, s->line, "cannot use continue inside a try except.");
		}
		discardScope(c, c->loops->depth);
		emitBytecode(c, OP_SIGN_CONT, s->line);
		emitShort(c, 0, 0);
		break;
	case BREAK_STMT:
		if(c->loops == NULL) {
			error(c, s->line, "cannot use break outside loop.");
			break;
		}
		if(c->tryDepth != 0) {
			error(c, s->line, "cannot use break inside a try except.");
		}
		discardScope(c, c->loops->depth);
		emitBytecode(c, OP_SIGN_BRK, s->line);
		emitShort(c, 0, 0);
		break;
	case EXCEPT_STMT:
		UNREACHABLE();
		break;
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

static void enterFunctionScope(Compiler *c) {
	c->depth++;
}

static void exitFunctionScope(Compiler *c) {
	c->depth--;
}

static ObjFunction *function(Compiler *c, ObjModule *module, Stmt *s) {
	size_t defaults = listLength(s->funcDecl.defArgs);
	size_t arity    = listLength(s->funcDecl.formalArgs);

	c->func = newFunction(c->vm, module, NULL, arity, defaults);
	addDefaultConsts(c, c->func->defaults, s->funcDecl.defArgs);

	if(s->funcDecl.id.length != 0) {
		c->func->name = copyString(c->vm,
			s->funcDecl.id.name, s->funcDecl.id.length, true);
	}

	enterFunctionScope(c);

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

	exitFunctionScope(c);

	return c->func;
}

static ObjFunction *method(Compiler *c, ObjModule *module, Identifier *classId, Stmt *s) {
	size_t defaults = listLength(s->funcDecl.defArgs);
	size_t arity    = listLength(s->funcDecl.formalArgs);

	c->func = newFunction(c->vm, module, NULL, arity, defaults);
	addDefaultConsts(c, c->func->defaults, s->funcDecl.defArgs);

	//create new method name by concatenating the class name to it
	size_t length = classId->length + s->funcDecl.id.length + 1;
	ObjString *name = allocateString(c->vm, length);

	memcpy(name->data, classId->name, classId->length);
	name->data[classId->length] = '.';
	memcpy(name->data + classId->length + 1, s->funcDecl.id.name, s->funcDecl.id.length);

	c->func->name = name;

	//if in costructor change the type
	Identifier ctor = {strlen(CTOR_STR), CTOR_STR};
	if(identifierEquals(&s->funcDecl.id, &ctor)) {
		c->type = TYPE_CTOR;
	}

	enterFunctionScope(c);

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

	exitFunctionScope(c);

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
			case 'e':  sbuf_appendchar(&sb, '\e'); break;  
			default:   sbuf_appendchar(&sb, str[i + 1]); break;
			}
			i++;
		} else {
			sbuf_appendchar(&sb, c);
	  	}
	}

	ObjString *s = copyString(c->vm, sbuf_get_backing_buf(&sb),  sbuf_get_len(&sb), true);
	sbuf_destroy(&sb);

	return s;
}

void reachCompilerRoots(BlangVM *vm, Compiler *c) {
	while(c != NULL) {
		reachObject(vm, (Obj*) c->func);
		c = c->prev;
	}
}
