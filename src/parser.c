#include "parser.h"
#include "token.h"
#include "linkedlist.h"

#include <stdlib.h>
#include <stdio.h>

#define match(parser, tokenType) (parser->peek.type == (tokenType))

static void advance(Parser *p);
static void require(Parser *p, TokenType type);
static void error(Parser *p, const char *msg);
static void synchronize(Parser *p);

static LinkedList *parseStmtOrDecl(Parser *p);

Stmt *parse(Parser *p, const char *src) {
	p->panic = false;
	p->hadError = false;
	p->prevType = -1;

	initLexer(&p->lex, src);
	nextToken(&p->lex, &p->peek);

	LinkedList *stmts = parseStmtOrDecl(p);

	if(!match(p, TOK_EOF))
		error(p, "unexpected token.");

	return newBlockStmt(0, stmts);
}

static Stmt *parseFuncDecl(Parser *p);
static Stmt *parseStmt(Parser *p);
static Stmt *blockStmt(Parser *p);
static Stmt *varDecl(Parser *p);

static LinkedList *parseStmtOrDecl(Parser *p) {
	LinkedList *stmts = NULL;

	while(!match(p, TOK_EOF)) {
		if(match(p, TOK_DEF)) {
			stmts = addElement(stmts, parseFuncDecl(p));
		} else if(match(p, TOK_VAR)) {
			stmts = addElement(stmts, varDecl(p));
		} else {
			stmts = addElement(stmts, parseStmt(p));
		}
	}

	return stmts;
}

Stmt *parseFuncDecl(Parser *p) {
	int line = p->peek.line;
	require(p, TOK_DEF);

	const char *name = NULL;
	size_t length = 0;

	if(match(p, TOK_IDENTIFIER)) {
		name = p->peek.lexeme;
		length = p->peek.length;
		advance(p);
	} else {
		error(p, "Expected identifier");
		advance(p);
	}

	require(p, TOK_LPAREN);
	LinkedList *args = NULL;

	if(match(p, TOK_IDENTIFIER)) {
		args = addElement(args, newIdentifier(p->peek.length, p->peek.lexeme));
		advance(p);
	}

	while(match(p, TOK_COMMA)) {
		advance(p);
		args = addElement(args, newIdentifier(p->peek.length, p->peek.lexeme));
		advance(p);
	}

	require(p, TOK_RPAREN);

	Stmt *body = blockStmt(p);

	return newFuncDecl(line, length, name, args, body);
}

//----- Statements parse ------

static Expr *parseExpr(Parser *p);

static Stmt *varDecl(Parser *p) {
	int line = p->peek.line;
	require(p, TOK_VAR);

	const char *name = NULL;
	size_t length = 0;

	if(match(p, TOK_IDENTIFIER)) {
		name = p->peek.lexeme;
		length = p->peek.length;
		advance(p);
	} else {
		error(p, "Expected identifier");
		advance(p);
	}

	Expr *init = NULL;
	if(match(p, TOK_EQUAL)) {
		advance(p);
		init = parseExpr(p);
	}

	require(p, TOK_SEMICOLON);

	return newVarDecl(line, name, length, init);
}

static Stmt *ifStmt(Parser *p) {
	int line = p->peek.line;
	require(p, TOK_IF);

	require(p, TOK_LPAREN);
	Expr *cond = parseExpr(p);
	require(p, TOK_RPAREN);

	Stmt *thenBody = parseStmt(p);
	Stmt *elseBody = NULL;

	if(match(p, TOK_ELSE)) {
		advance(p);
		elseBody = parseStmt(p);
	}

	return newIfStmt(line, cond, thenBody, elseBody);
}

static Stmt *whileStmt(Parser *p) {
	int line = p->peek.line;
	require(p, TOK_WHILE);

	require(p, TOK_LPAREN);
	Expr *cond = parseExpr(p);
	require(p, TOK_RPAREN);

	Stmt *body = parseStmt(p);

	return newWhileStmt(line, cond, body);
}

static Stmt *forStmt(Parser *p) {
	int line = p->peek.line;
	require(p, TOK_FOR);

	require(p, TOK_LPAREN);

	Expr *init = NULL;
	if(!match(p, TOK_SEMICOLON))
		init = parseExpr(p);

	require(p, TOK_SEMICOLON);

	Expr *cond = NULL;
	if(!match(p, TOK_SEMICOLON))
		cond = parseExpr(p);

	require(p, TOK_SEMICOLON);

	Expr *act = NULL;
	if(!match(p, TOK_RPAREN))
		act = parseExpr(p);

	require(p, TOK_RPAREN);

	Stmt *body = parseStmt(p);

	return newForStmt(line, init, cond, act, body);
}

static Stmt *returnStmt(Parser *p) {
	int line = p->peek.line;
	require(p, TOK_RETURN);

	Expr *e = NULL;
	if(!match(p, TOK_SEMICOLON)) {
		e = parseExpr(p);
	}
	require(p, TOK_SEMICOLON);

	return newReturnStmt(line, e);
}

static Stmt *blockStmt(Parser *p) {
	int line = p->peek.line;
	LinkedList *stmts = NULL;

	require(p, TOK_LBRACE);

	while(!match(p, TOK_RBRACE) && !match(p, TOK_EOF)) {
		if(match(p, TOK_VAR)) {
			stmts = addElement(stmts, varDecl(p));
		} else {
			stmts = addElement(stmts, parseStmt(p));
		}
	}

	require(p, TOK_RBRACE);

	return newBlockStmt(line, stmts);
}

static Stmt *parseStmt(Parser *p) {
	if(p->panic) synchronize(p);

	int line = p->peek.line;
	switch(p->peek.type) {
	case TOK_IF:
		return ifStmt(p);
	case TOK_FOR:
		return forStmt(p);
	case TOK_WHILE:
		return whileStmt(p);
	case TOK_RETURN:
		return returnStmt(p);
	case TOK_LBRACE:
		return blockStmt(p);
	default: {
		Expr *e = parseExpr(p);
		require(p, TOK_SEMICOLON);
		return newExprStmt(line, e);
	}
	}
}

static void synchronize(Parser *p) {
	p->panic = false;

	while(!match(p, TOK_EOF)) {
		if(p->prevType == TOK_SEMICOLON) {
			if(match(p, TOK_RBRACE)) advance(p);
			return;
		}

		switch(p->peek.type) {
		case TOK_DEF:
		case TOK_VAR:
		case TOK_FOR:
		case TOK_IF:
		case TOK_WHILE:
		case TOK_PRINT:
		case TOK_RETURN:
		case TOK_LBRACE:
			return;
		default: break;
		}

		advance(p);
	}
}

//----- Expressions parse ------

static Expr *literal(Parser *p) {
	int line = p->peek.line;
	switch(p->peek.type) {
	case TOK_NUMBER: {
		const char *end = p->peek.lexeme + p->peek.length;
		double num = strtod(p->peek.lexeme, (char **) &end);
		Expr *e = newNumLiteral(line, num);
		advance(p);
		return e;
	}
	case TOK_TRUE:
	case TOK_FALSE: {
		bool boolean = p->peek.type == TOK_TRUE ? true : false;
		Expr *e = newBoolLiteral(line, boolean);
		advance(p);
		return e;
	}
	case TOK_IDENTIFIER: {
		Expr *e = newVarLiteral(line, p->peek.lexeme, p->peek.length);
		advance(p);
		return e;
	}
	case TOK_STRING: {
		Expr *e = newStrLiteral(line, p->peek.lexeme, p->peek.length);
		advance(p);
		return e;
	}
	case TOK_NULL: {
		advance(p);
		return newNullLiteral(line);
	}
	case TOK_LPAREN: {
		require(p, TOK_LPAREN);
		Expr *e = parseExpr(p);
		require(p, TOK_RPAREN);
		return e;
	}
	default:
		error(p, "Expected expression");
		advance(p);
		break;
	}

	return NULL;
}

LinkedList *parseExprLst(Parser *p) {
	LinkedList *exprs = NULL;

	exprs = addElement(NULL, parseExpr(p));
	while(match(p, TOK_COMMA)) {
		advance(p);
		exprs = addElement(exprs, parseExpr(p));
	}

	return exprs;
}

static Expr *postfixExpr(Parser *p) {
	int line = p->peek.line;
	Expr *lit = literal(p);

	while(match(p, TOK_LPAREN)) {
		require(p, TOK_LPAREN);

		LinkedList *args = NULL;
		if(!match(p, TOK_RPAREN))
			args = parseExprLst(p);

		require(p, TOK_RPAREN);

		lit = newCallExpr(line, lit, args);
	}

	return lit;
}

static Expr *unaryExpr(Parser *p) {
	int line = p->peek.line;
	if(match(p, TOK_BANG)) {
		advance(p);
		return newUnary(line, NOT, unaryExpr(p));
	}
	if(match(p, TOK_MINUS)) {
		advance(p);
		return newUnary(line, MINUS, unaryExpr(p));
	}
	if(match(p, TOK_PLUS)) {
		advance(p);
		return newUnary(line, PLUS, unaryExpr(p));
	}

	return postfixExpr(p);
}

static Expr *multiplicativeExpr(Parser *p) {
	int line = p->peek.line;
	Expr *l = unaryExpr(p);

	while(match(p, TOK_MULT) || match(p, TOK_DIV) || match(p, TOK_MOD)) {
		advance(p);

		Expr *r = unaryExpr(p);

		switch(p->prevType) {
		case TOK_MULT:
			l = newBinary(line, MULT, l, r);
			break;
		case TOK_DIV:
			l = newBinary(line, DIV, l, r);
			break;
		case TOK_MOD:
			l = newBinary(line, MOD, l, r);
			break;
		default: break;
		}
	}

	return l;
}

static Expr *additiveExpr(Parser *p) {
	int line = p->peek.line;
	Expr *l = multiplicativeExpr(p);

	while(match(p, TOK_PLUS) || match(p, TOK_MINUS)) {
		advance(p);

		Expr *r = multiplicativeExpr(p);

		switch(p->prevType) {
		case TOK_PLUS:
			l = newBinary(line, PLUS, l, r);
			break;
		case TOK_MINUS:
			l = newBinary(line, MINUS, l, r);
			break;
		default: break;
		}
	}

	return l;
}

static Expr *relationalExpr(Parser *p) {
	int line = p->peek.line;
	Expr *l = additiveExpr(p);

	while(match(p, TOK_GT) || match(p, TOK_GE) ||
	 			match(p, TOK_LT) || match(p, TOK_LE)) {
		advance(p);

		Expr *r = additiveExpr(p);

		switch(p->prevType) {
		case TOK_GT:
			l = newBinary(line, GT, l, r);
			break;
		case TOK_GE:
			l = newBinary(line, GE, l, r);
			break;
		case TOK_LT:
			l = newBinary(line, LT, l, r);
			break;
		case TOK_LE:
			l = newBinary(line, LE, l, r);
			break;
		default: break;
		}
	}

	return l;
}

static Expr *equalityExpr(Parser *p) {
	int line = p->peek.line;
	Expr *l = relationalExpr(p);

	while(match(p, TOK_EQUAL_EQUAL) || match(p, TOK_BANG_EQ)) {
		advance(p);

		Expr *r = relationalExpr(p);

		switch(p->prevType) {
		case TOK_EQUAL_EQUAL:
			l = newBinary(line, EQ, l, r);
			break;
		case TOK_BANG_EQ:
			l = newBinary(line, NEQ, l, r);
			break;
		default: break;
		}
	}

	return l;
}

static Expr *logicAndExpr(Parser *p) {
	int line = p->peek.line;
	Expr *l = equalityExpr(p);

	while(match(p, TOK_AND)) {
		advance(p);
		Expr *r = equalityExpr(p);

		l = newBinary(line, AND, l, r);
	}

	return l;
}

static Expr *logicOrExpr(Parser *p) {
	int line = p->peek.line;
	Expr *l = logicAndExpr(p);

	while(match(p, TOK_OR)) {
		advance(p);
		Expr *r = logicAndExpr(p);

		l = newBinary(line, OR, l, r);
	}

	return l;
}

static Expr *parseExpr(Parser *p) {
	int line = p->peek.line;
	Expr *l = logicOrExpr(p);

	if(match(p, TOK_EQUAL)) {
		if(l->type != VAR_LIT) {
			error(p, "left hand side of assignment must be an lvalue.");
		}

		advance(p);
		Expr *r = parseExpr(p);
		l = newAssign(line, l, r);
	}

	return l;
}

static void error(Parser *p, const char *msg) {
	if(p->panic) return;

	fprintf(stderr, "[line:%d] error near or at %.*s: %s\n",
					p->peek.line, p->peek.length, p->peek.lexeme, msg);
	p->panic = true;
	p->hadError = true;
}

static void require(Parser *p, TokenType type) {
	if(match(p, type)) {
		advance(p);
		return;
	}

	//TODO: change
	char msg[1024];
	snprintf(msg, 1024, "expected token %s but instead %s found", tokNames[type], tokNames[p->peek.type]);
	error(p, msg);
}

static void advance(Parser *p) {
	p->prevType = p->peek.type;
	nextToken(&p->lex, &p->peek);

	while(match(p, TOK_ERR)) {
		p->hadError = true;
		fprintf(stderr, "[line:%d] invalid token: %.*s\n",
				p->peek.line, p->peek.length, p->peek.lexeme);
		nextToken(&p->lex, &p->peek);
	}
}
