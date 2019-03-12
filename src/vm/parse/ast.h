#ifndef AST_H
#define AST_H

#include <stdlib.h>
#include <stdbool.h>

#include "linkedlist.h"

typedef enum Operator {
	PLUS, MINUS, MULT, DIV, MOD, EQ, NEQ, AND, OR, NOT, GT, GE, LT, LE, IS
} Operator;

typedef enum ExprType {
	BINARY, UNARY, ASSIGN, NUM_LIT, BOOL_LIT, STR_LIT, VAR_LIT, NULL_LIT,
	EXPR_LST, CALL_EXPR, EXP_EXPR, SUPER_LIT, ACCESS_EXPR, ARR_LIT, ARR_ACC, 
	TERNARY, COMP_ASSIGN
} ExprType;

typedef struct Identifier {
	size_t length;
	const char *name;
} Identifier;

Identifier *newIdentifier(size_t length, const char *name);
bool identifierEquals(Identifier *id1, Identifier *id2);

typedef struct Expr Expr;
struct Expr {
	int line;
	ExprType type;
	union {
		struct {
			Operator op;
			Expr *left, *right;
		} bin;
		struct {
			Operator op;
			Expr *operand;
		} unary;
		struct {
			Expr *lval, *rval;
		} assign;
		struct {
			Operator op;
			Expr *lval, *rval;
		} compundAssign;
		struct {
			size_t length;
			const char *str;
		} str;
		struct {
			Identifier id;
		} var;
		struct {
			LinkedList *lst;
		} exprList;
		struct {
			Expr *callee, *args;
		} callExpr;
		struct {
			Expr *base, *exp;
		} expExpr;
		struct {
			Expr *left;
			Identifier id;
		} accessExpr;
		struct {
			Expr *left;
			Expr *index;
		} arrAccExpr;
		struct {
			Expr *exprs;
		} arr;
		struct {
			Expr *cond;
			Expr *thenExpr;
			Expr *elseExpr;
		} ternary;
		double num;
		bool boolean;
	};
};

Expr *newBinary(int line, Operator op, Expr *l, Expr *r);
Expr *newAssign(int line, Expr *lval, Expr *rval);
Expr *newUnary(int line, Operator op, Expr *operand);
Expr *newNullLiteral(int line);
Expr *newSuperLiteral(int line);
Expr *newNumLiteral(int line, double num);
Expr *newBoolLiteral(int line, bool boolean);
Expr *newArrayAccExpr(int line, Expr *left, Expr *index);
Expr *newStrLiteral(int line, const char *str, size_t len);
Expr *newVarLiteral(int line, const char *str, size_t len);
Expr *newArrLiteral(int line, Expr *exprs);
Expr *newExprList(int line, LinkedList *exprs);
Expr *newCallExpr(int line, Expr *callee, LinkedList *args);
Expr *newExpExpr(int line, Expr *base, Expr *exp);
Expr *newAccessExpr(int line, Expr *left, const char *name, size_t length);
Expr *newTernary(int line, Expr *cond, Expr *thenExpr, Expr *elseExpr);
Expr *newCompoundAssing(int line, Operator op, Expr *lval, Expr *rval);

void freeExpr(Expr *e);

typedef enum StmtType {
	IF, FOR, WHILE, FOREACH, BLOCK, RETURN_STMT, EXPR, VARDECL, FUNCDECL,
	NATIVEDECL, CLASSDECL, IMPORT, TRY_STMT, EXCEPT_STMT, RAISE_STMT,
	CONTINUE_STMT, BREAK_STMT
} StmtType;

typedef struct Stmt Stmt;

struct Stmt {
	int line;
	StmtType type;
	union {
		struct {
			Expr *cond;
			Stmt *thenStmt, *elseStmt;
		} ifStmt;
		struct {
			Stmt *init;
			Expr *cond, *act;
			Stmt *body;
		} forStmt;
		struct {
			Stmt *var;
			Expr *iterable;
			Stmt *body;
		} forEach;
		struct {
			Expr *cond;
			Stmt *body;
		} whileStmt;
		struct {
			Expr *e;
		} returnStmt;
		struct {
			LinkedList *stmts;
		} blockStmt;
		struct {
			Identifier id;
			Expr *init;
		} varDecl;
		struct {
			Identifier id;
			LinkedList *formalArgs, *defArgs;
			Stmt *body;
		} funcDecl;
		struct {
			Identifier id;
			LinkedList *formalArgs, *defArgs;
		} nativeDecl;
		struct {
			Identifier id;
			Expr *sup;
			LinkedList *methods;
		} classDecl;
		struct {
			LinkedList *modules;
			Identifier as;
			LinkedList *impNames;
		} importStmt;
		struct {
			Stmt *block;
			LinkedList *excs;
			Stmt *ensure;
		} tryStmt;
		struct {
			Expr *cls;
			Identifier var;
			Stmt *block;
		} excStmt;
		struct {
			Expr *exc;
		} raiseStmt;
		Expr *exprStmt;
	};
};

Stmt *newFuncDecl(int line, size_t length, const char *id, LinkedList *args, LinkedList *defArgs, Stmt *body);
Stmt *newImportStmt(int line, LinkedList *modules, LinkedList *impNames, const char *as, size_t asLength);
Stmt *newNativeDecl(int line, size_t length, const char *id, LinkedList *args, LinkedList *defArgs);
Stmt *newClassDecl(int line, size_t clength, const char *cid, Expr *sup, LinkedList *methods);
Stmt *newExceptStmt(int line, Expr *cls, size_t vlen, const char *var, Stmt *block);
Stmt *newForStmt(int line, Stmt *init, Expr *cond, Expr *act, Stmt *body);
Stmt *newVarDecl(int line, const char *name, size_t length, Expr *init);
Stmt *newTryStmt(int line, Stmt *blck, LinkedList *excs, Stmt *ensure);
Stmt *newIfStmt(int line, Expr *cond, Stmt *thenStmt, Stmt *elseStmt);
Stmt *newForEach(int line, Stmt *varDecl, Expr *iter, Stmt *body);
Stmt *newWhileStmt(int line, Expr *cond, Stmt *body);
Stmt *newBlockStmt(int line, LinkedList *list);
Stmt *newReturnStmt(int line, Expr *e);
Stmt *newRaiseStmt(int line, Expr *e);
Stmt *newExprStmt(int line, Expr *e);
Stmt *newContinueStmt(int line);
Stmt *newBreakStmt(int line);

void freeStmt(Stmt *s);

#endif
