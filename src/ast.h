#ifndef AST_H
#define AST_H

#include <stdlib.h>
#include <stdbool.h>

#include "linkedlist.h"

typedef enum Operator {
	PLUS, MINUS, MULT, DIV, MOD, EQ, NEQ, AND, OR, NOT, GT, GE, LT, LE
} Operator;

typedef enum ExprType {
	BINARY, UNARY, ASSIGN, NUM_LIT, STR_LIT, VAR_LIT, NULL_LIT, EXPR_LST,
	CALL_EXPR
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
		double num;
	};
};

Expr *newBinary(int line, Operator op, Expr *l, Expr *r);
Expr *newAssign(int line, Expr *lval, Expr *rval);
Expr *newUnary(int line, Operator op, Expr *operand);
Expr *newNullLiteral(int line);
Expr *newNumLiteral(int line, double num);
Expr *newStrLiteral(int line, const char *str, size_t len);
Expr *newVarLiteral(int line, const char *str, size_t len);
Expr *newExprList(int line, LinkedList *exprs);
Expr *newCallExpr(int line, Expr *callee, LinkedList *args);

void freeExpr(Expr *e);

typedef enum StmtType {
	IF, FOR, WHILE, BLOCK, RETURN, EXPR, VARDECL, FUNCDECL
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
			Expr *init, *cond, *act;
			Stmt *body;
		} forStmt;
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
			LinkedList *formalArgs;
			Stmt *body;
		} funcDecl;
		Expr *exprStmt;
	};
};

Stmt *newFuncDecl(int line, size_t length, const char *id, LinkedList *args, Stmt *body);
Stmt *newForStmt(int line, Expr *init, Expr *cond, Expr *act, Stmt *body);
Stmt *newVarDecl(int line, const char *name, size_t length, Expr *init);
Stmt *newIfStmt(int line, Expr *cond, Stmt *thenStmt, Stmt *elseStmt);
Stmt *newWhileStmt(int line, Expr *cond, Stmt *body);
Stmt *newReturnStmt(int line, Expr *e);
Stmt *newBlockStmt(int line, LinkedList *list);
Stmt *newExprStmt(int line, Expr *e);

void freeStmt(Stmt *s);

#endif
