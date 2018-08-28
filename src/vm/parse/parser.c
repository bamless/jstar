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

static Stmt *parseProgram(Parser *p);

Stmt *parse(Parser *p, const char *src) {
	p->panic = false;
	p->hadError = false;
	p->prevType = -1;

	initLexer(&p->lex, src);
	nextToken(&p->lex, &p->peek);

	Stmt *program = parseProgram(p);

	if(!match(p, TOK_EOF))
		error(p, "Unexpected token.");

	return program;
}

static Stmt *parseNativeDecl(Parser *p);
static Stmt *parseFuncDecl(Parser *p);
static Stmt *parseStmt(Parser *p);
static Stmt *blockStmt(Parser *p);
static Stmt *varDecl(Parser *p);

static Stmt *parseProgram(Parser *p) {
	LinkedList *stmts = NULL;

	while(!match(p, TOK_EOF)) {
		if(match(p, TOK_DEF)) {
			stmts = addElement(stmts, parseFuncDecl(p));
		} else if(match(p, TOK_NAT)) {
			stmts = addElement(stmts, parseNativeDecl(p));
		} else if(match(p, TOK_VAR)) {
			stmts = addElement(stmts, varDecl(p));
			require(p, TOK_SEMICOLON);
		} else {
			stmts = addElement(stmts, parseStmt(p));
		}

		if(p->panic) synchronize(p);
	}

	return newFuncDecl(0, 0, NULL, NULL, newBlockStmt(0, stmts));
}

static Stmt *parseNativeDecl(Parser *p) {
	int line = p->peek.line;
	require(p, TOK_NAT);

	const char *name = NULL;
	size_t length = 0;

	if(match(p, TOK_IDENTIFIER)) {
		name = p->peek.lexeme;
		length = p->peek.length;
		advance(p);
	} else {
		error(p, "Expected identifier.");
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
	require(p, TOK_SEMICOLON);

	return newNativeDecl(line, length, name, args);
}

static Stmt *parseFuncDecl(Parser *p) {
	int line = p->peek.line;
	require(p, TOK_DEF);

	const char *name = NULL;
	size_t length = 0;

	if(match(p, TOK_IDENTIFIER)) {
		name = p->peek.lexeme;
		length = p->peek.length;
		advance(p);
	} else {
		error(p, "Expected identifier.");
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

static Expr *parseExpr(Parser *p);

static Stmt *parseClassDecl(Parser *p) {
	int line = p->peek.line;
	require(p, TOK_CLASS);

	const char *cname = NULL;
	size_t clength = 0;
	if(match(p, TOK_IDENTIFIER)) {
		cname = p->peek.lexeme;
		clength = p->peek.length;
		advance(p);
	} else {
		error(p, "Expected class name.");
		advance(p);
	}

	Expr *sup = NULL;
	if(match(p, TOK_COLON)) {
		require(p, TOK_COLON);
		sup = parseExpr(p);
	}

	require(p, TOK_LBRACE);

	LinkedList *methods = NULL;
	while(!match(p, TOK_RBRACE) && !match(p, TOK_EOF)) {
		if(match(p, TOK_DEF)) {
			methods = addElement(methods, parseFuncDecl(p));
		} else {
			methods = addElement(methods, parseNativeDecl(p));
		}
	}

	require(p, TOK_RBRACE);

	return newClassDecl(line, clength, cname, sup, methods);
}

//----- Statements parse ------

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
		error(p, "Expected identifier.");
		advance(p);
	}

	Expr *init = NULL;
	if(match(p, TOK_EQUAL)) {
		advance(p);
		init = parseExpr(p);
	}

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

	if(match(p, TOK_IDENTIFIER)) {

	}

	require(p, TOK_LPAREN);

	Stmt *init = NULL;
	if(!match(p, TOK_SEMICOLON)) {
		if(match(p, TOK_VAR)) {
			init = varDecl(p);

			//if we dont have a semicolon we're parsing a foreach
			if(!match(p, TOK_SEMICOLON)) {
				if(init->varDecl.init != NULL) {
					error(p, "Variable declaration in for each cannot have initializer.");
				}
				require(p, TOK_IN);

				Expr *e = parseExpr(p);
				require(p, TOK_RPAREN);

				Stmt *body = parseStmt(p);

				return newForEach(line, init, e, body);
			}
		} else {
			Expr *e = parseExpr(p);
			if(e != NULL) init = newExprStmt(e->line, e);
			require(p, TOK_SEMICOLON);
		}
	}

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
			require(p, TOK_SEMICOLON);
		} else {
			stmts = addElement(stmts, parseStmt(p));
		}
	}

	require(p, TOK_RBRACE);

	return newBlockStmt(line, stmts);
}

static Stmt *parseImport(Parser *p) {
	int line = p->peek.line;
	require(p, TOK_IMPORT);

	const char *name = NULL;
	size_t length = 0;
	if(match(p, TOK_IDENTIFIER)) {
		name = p->peek.lexeme;
		length = p->peek.length;
	} else {
		error(p, "Expected module name.");
	}
	advance(p);

	const char *as = NULL;
	size_t asLength = 0;
	if(match(p, TOK_AS)) {
		advance(p);

		if(match(p, TOK_IDENTIFIER)) {
			as = p->peek.lexeme;
			asLength = p->peek.length;
		} else {
			error(p, "Expected module name.");
		}
		advance(p);
	}

	require(p, TOK_SEMICOLON);

	return newImportStmt(line, name, length, as, asLength);
}

static Stmt *parseTryStmt(Parser *p) {
	int line = p->peek.line;
	require(p, TOK_TRY);

	Stmt *tryBlock = blockStmt(p);
	LinkedList *excs = NULL;

	do {
		int excLine = p->peek.line;

		const char *var = NULL;
		size_t varLen = 0;

		require(p, TOK_EXCEPT);
		require(p, TOK_LPAREN);

		Expr *cls = parseExpr(p);

		if(match(p, TOK_IDENTIFIER)) {
			var = p->peek.lexeme;
			varLen = p->peek.length;
		} else {
			error(p, "Expected identifier.");
		}

		advance(p);
		require(p, TOK_RPAREN);

		Stmt *blck = blockStmt(p);
		excs = addElement(excs, newExceptStmt(excLine, cls, varLen, var, blck));
	} while(match(p, TOK_EXCEPT));

	return newTryStmt(line, tryBlock, excs);
}

static Stmt *parseRaiseStmt(Parser *p) {
	int line = p->peek.line;
	advance(p);

	Expr *exc = parseExpr(p);
	require(p, TOK_SEMICOLON);
	
	return newRaiseStmt(line, exc);
}

static Stmt *parseStmt(Parser *p) {
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
	case TOK_CLASS:
		return parseClassDecl(p);
	case TOK_IMPORT:
		return parseImport(p);
	case TOK_TRY:
		return parseTryStmt(p);
	case TOK_RAISE:
		return parseRaiseStmt(p);
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

LinkedList *parseExprLst(Parser *p) {
	LinkedList *exprs = NULL;

	exprs = addElement(NULL, parseExpr(p));
	while(match(p, TOK_COMMA)) {
		advance(p);
		exprs = addElement(exprs, parseExpr(p));
	}

	return exprs;
}

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
	case TOK_SUPER: {
		advance(p);
		return newSuperLiteral(line);
	}
	case TOK_LPAREN: {
		require(p, TOK_LPAREN);
		Expr *e = parseExpr(p);
		require(p, TOK_RPAREN);
		return e;
	}
	case TOK_LSQUARE: {
		require(p, TOK_LSQUARE);
		LinkedList *exprs = NULL;
		if(!match(p, TOK_RSQUARE))
			exprs = parseExprLst(p);
		require(p, TOK_RSQUARE);
		return newArrLiteral(line, newExprList(line, exprs));
	}
	case TOK_ERR:
		error(p, "Unexpected token");
		advance(p);
		break;
	default:
		error(p, "Expected expression.");
		advance(p);
		break;
	}

	return NULL;
}

static Expr *postfixExpr(Parser *p) {
	Expr *lit = literal(p);

	while(match(p, TOK_LPAREN) || match(p, TOK_DOT) || match(p, TOK_LSQUARE)) {
		int line = p->peek.line;
		switch(p->peek.type) {
		case TOK_DOT: {
			require(p, TOK_DOT);
			if(p->peek.type != TOK_IDENTIFIER) error(p, "Expected identifier");
			lit = newAccessExpr(line, lit, p->peek.lexeme, p->peek.length);
			advance(p);
			break;
		}
		case TOK_LPAREN: {
			require(p, TOK_LPAREN);

			LinkedList *args = NULL;
			if(!match(p, TOK_RPAREN))
				args = parseExprLst(p);

			require(p, TOK_RPAREN);
			lit = newCallExpr(line, lit, args);
			break;
		}
		case TOK_LSQUARE: {
			require(p, TOK_LSQUARE);
			lit = newArrayAccExpr(line, lit, parseExpr(p));
			require(p, TOK_RSQUARE);
			break;
		}
		default: break;
		}

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

	return postfixExpr(p);
}

static Expr *multiplicativeExpr(Parser *p) {
	Expr *l = unaryExpr(p);

	while(match(p, TOK_MULT) || match(p, TOK_DIV) || match(p, TOK_MOD)) {
		int line = p->peek.line;
		TokenType tokType = p->peek.type;
		advance(p);

		Expr *r = unaryExpr(p);

		switch(tokType) {
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
	Expr *l = multiplicativeExpr(p);

	while(match(p, TOK_PLUS) || match(p, TOK_MINUS)) {
		int line = p->peek.line;
		TokenType tokType = p->peek.type;
		advance(p);

		Expr *r = multiplicativeExpr(p);

		switch(tokType) {
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
	Expr *l = additiveExpr(p);

	while(match(p, TOK_GT) || match(p, TOK_GE) ||
			match(p, TOK_LT) || match(p, TOK_LE) || match(p, TOK_IS)) {
		int line = p->peek.line;
		TokenType tokType = p->peek.type;
		advance(p);

		Expr *r = additiveExpr(p);

		switch(tokType) {
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
		case TOK_IS:
			l = newBinary(line, IS, l, r);
		default: break;
		}
	}

	return l;
}

static Expr *equalityExpr(Parser *p) {
	Expr *l = relationalExpr(p);

	while(match(p, TOK_EQUAL_EQUAL) || match(p, TOK_BANG_EQ)) {
		int line = p->peek.line;
		TokenType tokType = p->peek.type;
		advance(p);

		Expr *r = relationalExpr(p);

		switch(tokType) {
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
	Expr *l = equalityExpr(p);

	while(match(p, TOK_AND)) {
		int line = p->peek.line;
		advance(p);
		Expr *r = equalityExpr(p);

		l = newBinary(line, AND, l, r);
	}

	return l;
}

static Expr *logicOrExpr(Parser *p) {
	Expr *l = logicAndExpr(p);

	while(match(p, TOK_OR)) {
		int line = p->peek.line;
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
		if(l != NULL && l->type != VAR_LIT && l->type != ACCESS_EXPR &&
			l->type != ARR_ACC) {
			error(p, "Left hand side of assignment must be an lvalue.");
		}

		advance(p);
		Expr *r = parseExpr(p);
		l = newAssign(line, l, r);
	}

	return l;
}

static void error(Parser *p, const char *msg) {
	if(p->panic) return;

	fprintf(stderr, "[line:%d] Error near or at `%.*s`: %s\n",
					p->peek.line, p->peek.length, p->peek.lexeme, msg);
	p->panic = true;
	p->hadError = true;
}

static void require(Parser *p, TokenType type) {
	if(match(p, type)) {
		advance(p);
		return;
	}

	char msg[1025] = {'\0'};
	snprintf(msg, 1024, "Expected token `%s` but instead `%s` found.",
		tokNames[type], tokNames[p->peek.type]);
	error(p, msg);
}

static void advance(Parser *p) {
	p->prevType = p->peek.type;
	nextToken(&p->lex, &p->peek);

	while(match(p, TOK_ERR)) {
		p->hadError = true;
		fprintf(stderr, "[line:%d] Invalid token: %.*s\n",
				p->peek.line, p->peek.length, p->peek.lexeme);
		nextToken(&p->lex, &p->peek);
	}
}
