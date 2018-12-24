#include "ast.h"

#include <stdlib.h>
#include <string.h>

Identifier *newIdentifier(size_t length, const char *name) {
	Identifier *id = malloc(sizeof(*id));
	id->length = length;
	id->name = name;
	return id;
}

bool identifierEquals(Identifier *id1, Identifier *id2) {
	return id1->length == id2->length &&
		(memcmp(id1->name, id2->name, id1->length) == 0);
}

//----- Expressions -----

Expr *newBinary(int line, Operator op, Expr *l, Expr *r) {
	Expr *e = malloc(sizeof(*e));
	e->line = line;
	e->type = BINARY;
	e->bin.op = op;
	e->bin.left = l;
	e->bin.right = r;
	return e;
}

Expr *newAssign(int line, Expr *lval, Expr *rval) {
	Expr *e = malloc(sizeof(*e));
	e->line = line;
	e->type = ASSIGN;
	e->assign.lval = lval;
	e->assign.rval = rval;
	return e;
}

Expr *newUnary(int line, Operator op, Expr *operand) {
	Expr *e = malloc(sizeof(*e));
	e->line = line;
	e->type = UNARY;
	e->unary.op = op;
	e->unary.operand = operand;
	return e;
}

Expr *newNullLiteral(int line) {
	Expr *e = malloc(sizeof(*e));
	e->line = line;
	e->type = NULL_LIT;
	return e;
}

Expr *newNumLiteral(int line, double num) {
	Expr *e = malloc(sizeof(*e));
	e->line = line;
	e->type = NUM_LIT;
	e->num = num;
	return e;
}

Expr *newBoolLiteral(int line, bool boolean) {
	Expr *e = malloc(sizeof(*e));
	e->line = line;
	e->type = BOOL_LIT;
	e->boolean = boolean;
	return e;
}

Expr *newStrLiteral(int line, const char *str, size_t len) {
	Expr *e = malloc(sizeof(*e));
	e->line = line;
	e->type = STR_LIT;
	e->str.str = str;
	e->str.length = len;
	return e;
}

Expr *newVarLiteral(int line, const char *var, size_t len) {
	Expr *e = malloc(sizeof(*e));
	e->line = line;
	e->type = VAR_LIT;
	e->var.id.name = var;
	e->var.id.length = len;
	return e;
}

Expr *newArrLiteral(int line, Expr *exprs) {
	Expr *a = malloc(sizeof(*a));
	a->line = line;
	a->type = ARR_LIT;
	a->arr.exprs = exprs;
	return a;
}

Expr *newExprList(int line, LinkedList *exprs) {
	Expr *e = malloc(sizeof(*e));
	e->line = line;
	e->type = EXPR_LST;
	e->exprList.lst = exprs;
	return e;
}

Expr *newCallExpr(int line, Expr *callee, LinkedList *args) {
	Expr *e = malloc(sizeof(*e));
	e->line = line;
	e->type = CALL_EXPR;
	e->callExpr.callee = callee;
	e->callExpr.args = newExprList(line, args);
	return e;
}

Expr *newSuperLiteral(int line) {
	Expr *e = malloc(sizeof(*e));
	e->line = line;
	e->type = SUPER_LIT;
	e->num = 0;
	return e;
}

Expr *newAccessExpr(int line, Expr *left, const char *name, size_t length) {
	Expr *e = malloc(sizeof(*e));
	e->line = line;
	e->type = ACCESS_EXPR;
	e->accessExpr.left = left;
	e->accessExpr.id.name = name;
	e->accessExpr.id.length = length;
	return e;
}

Expr *newArrayAccExpr(int line, Expr *left, Expr *index) {
	Expr *e = malloc(sizeof(*e));
	e->line = line;
	e->type = ARR_ACC;
	e->arrAccExpr.left = left;
	e->arrAccExpr.index = index;
	return e;
}

void freeExpr(Expr *e) {
	if(e == NULL) return;

	switch(e->type) {
	case BINARY:
		freeExpr(e->bin.left);
		freeExpr(e->bin.right);
		break;
	case UNARY:
		freeExpr(e->unary.operand);
		break;
	case ASSIGN:
		freeExpr(e->assign.lval);
		freeExpr(e->assign.rval);
		break;
	case ARR_LIT: {
		freeExpr(e->arr.exprs);
		break;
	}
	case EXPR_LST: {
		LinkedList *head = e->exprList.lst;
		while(head != NULL) {
			LinkedList *f = head;
			head = head->next;
			freeExpr(f->elem);
			free(f);
		}
		break;
	}
	case CALL_EXPR:
		freeExpr(e->callExpr.callee);
		freeExpr(e->callExpr.args);
		break;
	case ACCESS_EXPR:
		freeExpr(e->accessExpr.left);
		break;
	case ARR_ACC:
		freeExpr(e->arrAccExpr.left);
		freeExpr(e->arrAccExpr.index);
		break;
	default: break;
	}

	free(e);
}

//----- Statements -----

Stmt *newFuncDecl(int line, size_t length, const char *id, LinkedList *args, Stmt *body) {
	Stmt *f = malloc(sizeof(*f));
	f->line = line;
	f->type = FUNCDECL;
	f->funcDecl.id.name = id;
	f->funcDecl.id.length = length;
	f->funcDecl.formalArgs = args;
	f->funcDecl.body = body;
	return f;
}

Stmt *newNativeDecl(int line, size_t length, const char *id, LinkedList *args) {
	Stmt *n = malloc(sizeof(*n));
	n->line = line;
	n->type = NATIVEDECL;
	n->nativeDecl.id.name = id;
	n->nativeDecl.id.length = length;
	n->nativeDecl.formalArgs = args;
	return n;
}

Stmt *newClassDecl(int line, size_t clength, const char *cid, Expr *sup, LinkedList *methods) {
	Stmt *c = malloc(sizeof(*c));
	c->line = line;
	c->type = CLASSDECL;
	c->classDecl.sup = sup;
	c->classDecl.id.name = cid;
	c->classDecl.id.length = clength;
	c->classDecl.methods = methods;
	return c;
}

Stmt *newForStmt(int line, Stmt *init, Expr *cond, Expr *act, Stmt *body) {
	Stmt *s = malloc(sizeof(*s));
	s->line = line;
	s->type = FOR;
	s->forStmt.init = init;
	s->forStmt.cond = cond;
	s->forStmt.act = act;
	s->forStmt.body = body;
	return s;
}

Stmt *newForEach(int line, Stmt *var, Expr *iter, Stmt *body) {
	Stmt *s = malloc(sizeof(*s));
	s->line = line;
	s->type = FOREACH;
	s->forEach.var = var;
	s->forEach.iterable = iter;
	s->forEach.body = body;
	return s;
}

Stmt *newVarDecl(int line, const char *name, size_t length, Expr *init) {
	Stmt *s = malloc(sizeof(*s));
	s->line = line;
	s->type = VARDECL;
	s->varDecl.id.name = name;
	s->varDecl.id.length = length;
	s->varDecl.init = init;
	return s;
}

Stmt *newWhileStmt(int line, Expr *cond, Stmt *body) {
	Stmt *s = malloc(sizeof(*s));
	s->line = line;
	s->type = WHILE;
	s->whileStmt.cond = cond;
	s->whileStmt.body = body;
	return s;
}

Stmt *newReturnStmt(int line, Expr *e) {
	Stmt *s = malloc(sizeof(*s));
	s->line = line;
	s->type = RETURN_STMT;
	s->returnStmt.e = e;
	return s;
}

Stmt *newIfStmt(int line, Expr *cond, Stmt *thenStmt, Stmt *elseStmt) {
	Stmt *s = malloc(sizeof(*s));
	s->line = line;
	s->type = IF;
	s->ifStmt.cond = cond;
	s->ifStmt.thenStmt = thenStmt;
	s->ifStmt.elseStmt = elseStmt;
	return s;
}

Stmt *newBlockStmt(int line, LinkedList *list) {
	Stmt *s = malloc(sizeof(*s));
	s->line = line;
	s->type = BLOCK;
	s->blockStmt.stmts = list;
	return s;
}

Stmt *newImportStmt(int line, const char *module, size_t length, const char *as, size_t asLength) {
	Stmt *s = malloc(sizeof(*s));
	s->line = line;
	s->type = IMPORT;
	s->importStmt.module.name = module;
	s->importStmt.module.length = length;
	s->importStmt.as.name = as;
	s->importStmt.as.length = asLength;
	return s;
}

Stmt *newExprStmt(int line, Expr *e) {
	Stmt *s = malloc(sizeof(*s));
	s->line = line;
	s->type = EXPR;
	s->exprStmt = e;
	return s;
}

Stmt *newTryStmt(int line, Stmt *blck, LinkedList *excs) {
	Stmt *s = malloc(sizeof(*s));
	s->line = line;
	s->type = TRY_STMT;
	s->tryStmt.block = blck;
	s->tryStmt.excs = excs;
	return s;
}

Stmt *newExceptStmt(int line, Expr *cls, size_t vlen, const char *var, Stmt *block) {
	Stmt *s = malloc(sizeof(*s));
	s->line = line;
	s->type = EXCEPT_STMT;
	s->excStmt.block = block;
	s->excStmt.cls = cls;
	s->excStmt.var.length = vlen;
	s->excStmt.var.name = var;
	return s;
}

Stmt *newRaiseStmt(int line, Expr *e) {
	Stmt *s = malloc(sizeof(*s));
	s->line = line;
	s->type = RAISE_STMT;
	s->raiseStmt.exc = e;
	return s;
}

Stmt *newContinueStmt(int line) {
	Stmt *s = malloc(sizeof(*s));
	s->line = line;
	s->type = CONTINUE_STMT;
	s->exprStmt = NULL;
	return s;
}

Stmt *newBreakStmt(int line) {
	Stmt *s = malloc(sizeof(*s));
	s->line = line;
	s->type = BREAK_STMT;
	s->exprStmt = NULL;
	return s;
}

void freeStmt(Stmt *s) {
	if(s == NULL) return;

	switch (s->type) {
	case IF:
		freeExpr(s->ifStmt.cond);
		freeStmt(s->ifStmt.thenStmt);
		freeStmt(s->ifStmt.elseStmt);
		break;
	case FOR:
		freeStmt(s->forStmt.init);
		freeExpr(s->forStmt.cond);
		freeExpr(s->forStmt.act);
		freeStmt(s->forStmt.body);
		break;
	case FOREACH:
		freeStmt(s->forEach.var);
		freeExpr(s->forEach.iterable);
		freeStmt(s->forEach.body);
		break;
	case WHILE:
		freeExpr(s->whileStmt.cond);
		freeStmt(s->whileStmt.body);
		break;
	case RETURN_STMT:
		freeExpr(s->returnStmt.e);
		break;
	case EXPR:
		freeExpr(s->exprStmt);
		break;
	case BLOCK: {
		LinkedList *head = s->blockStmt.stmts;
		while(head != NULL) {
			LinkedList *f = head;
			head = head->next;
			freeStmt(f->elem);
			free(f);
		}
		break;
	}
	case FUNCDECL: {
		LinkedList *head = s->funcDecl.formalArgs;
		while(head != NULL) {
			LinkedList *f = head;
			head = head->next;
			free(f->elem);
			free(f);
		}
		freeStmt(s->funcDecl.body);
		break;
	}
	case NATIVEDECL: {
		LinkedList *head = s->nativeDecl.formalArgs;
		while(head != NULL) {
			LinkedList *f = head;
			head = head->next;
			free(f->elem);
			free(f);
		}
		break;
	}
	case CLASSDECL: {
		freeExpr(s->classDecl.sup);
		LinkedList *head = s->classDecl.methods;
		while(head != NULL) {
			LinkedList *f = head;
			head = head->next;
			freeStmt((Stmt*)f->elem);
			free(f);
		}
		break;
	}
	case VARDECL:
		freeExpr(s->varDecl.init);
		break;
	case TRY_STMT:
		freeStmt(s->tryStmt.block);
		LinkedList *head = s->tryStmt.excs;
		while(head != NULL) {
			LinkedList *f = head;
			head = head->next;
			freeStmt(f->elem);
			free(f);
		}
		break;
	case EXCEPT_STMT:
		freeExpr(s->excStmt.cls);
		freeStmt(s->excStmt.block);
		break;
	case RAISE_STMT:
		freeExpr(s->raiseStmt.exc);
		break;
	case CONTINUE_STMT:
	case BREAK_STMT:
	case IMPORT:
		break;
	}

	free(s);
}
