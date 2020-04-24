#include "jsrparse/parser.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "jsrparse/ast.h"
#include "jsrparse/lex.h"
#include "jsrparse/linkedlist.h"
#include "jsrparse/token.h"

typedef struct Parser {
    Lexer lex;
    Token peek;
    const char* fname;
    TokenType prevType;
    const char* lnStart;
    bool panic, hadError;
} Parser;

static void initParser(Parser* p, const char* fname, const char* src) {
    p->panic = false;
    p->hadError = false;
    p->fname = fname;
    p->prevType = -1;
    initLexer(&p->lex, src);
    nextToken(&p->lex, &p->peek);
    p->lnStart = p->peek.lexeme;
}

//----- Utility functions ------

static char* strchrnul(const char* str, char c) {
    char* ret;
    return (ret = strchr(str, c)) == NULL ? strchr(str, '\0') : ret;
}

static void error(Parser* p, const char* msg, ...) {
    if(p->panic) return;
    p->panic = p->hadError = true;

    if(p->fname != NULL) {
        fprintf(stderr, "File %s [line:%d]:\n", p->fname, p->peek.line);

        int tokOff = (int)((p->peek.lexeme) - p->lnStart);
        int lineLen = (int)(strchrnul(p->peek.lexeme, '\n') - p->lnStart);

        fprintf(stderr, "    %.*s\n", lineLen, p->lnStart);
        fprintf(stderr, "    ");
        for(int i = 0; i < tokOff; i++) {
            fprintf(stderr, " ");
        }
        fprintf(stderr, "^\n");

        va_list args;
        va_start(args, msg);
        vfprintf(stderr, msg, args);
        va_end(args);
        fprintf(stderr, "\n");
    }
}

static bool match(Parser* p, TokenType type) {
    return p->peek.type == type;
}

static void advance(Parser* p) {
    p->prevType = p->peek.type;
    nextToken(&p->lex, &p->peek);

    if(p->prevType == TOK_NEWLINE) {
        p->lnStart = p->peek.lexeme;
    }

    while(match(p, TOK_ERR) || match(p, TOK_UNTERMINATED_STR)) {
        error(p, p->peek.type == TOK_ERR ? "Invalid token." : "Unterminated string.");
        nextToken(&p->lex, &p->peek);
    }
}

static void skipNewLines(Parser* p) {
    while(p->peek.type == TOK_NEWLINE) {
        advance(p);
    }
}

static Token require(Parser* p, TokenType type) {
    if(match(p, type)) {
        Token t = p->peek;
        advance(p);
        return t;
    }
    error(p, "Expected token `%s`, instead `%s` found.", tokNames[type], tokNames[p->peek.type]);
    return (Token){0, NULL, 0, 0};
}

static void synchronize(Parser* p) {
    p->panic = false;
    while(!match(p, TOK_EOF)) {
        switch(p->peek.type) {
        case TOK_FUN:
        case TOK_VAR:
        case TOK_FOR:
        case TOK_IF:
        case TOK_WHILE:
        case TOK_RETURN:
        case TOK_THEN:
        case TOK_DO:
        case TOK_BEGIN:
        case TOK_CLASS:
            return;
        default:
            break;
        }
        advance(p);
    }
}

static void classSynchronize(Parser* p) {
    p->panic = false;
    while(!match(p, TOK_EOF)) {
        switch(p->peek.type) {
        case TOK_FUN:
        case TOK_END:
            return;
        default:
            break;
        }
        advance(p);
    }
}

static bool isImplicitEnd(Token* tok) {
    TokenType t = tok->type;
    return t == TOK_EOF || t == TOK_END || t == TOK_ELSE || t == TOK_ELIF || t == TOK_ENSURE ||
           t == TOK_EXCEPT;
}

static bool isStatementEnd(Token* tok) {
    return isImplicitEnd(tok) || tok->type == TOK_NEWLINE || tok->type == TOK_SEMICOLON;
}

static bool isLValue(ExprType type) {
    return type == VAR_LIT || type == ACCESS_EXPR || type == ARR_ACC;
}

static bool isConstantLiteral(ExprType type) {
    return type == NUM_LIT || type == BOOL_LIT || type == STR_LIT || type == NULL_LIT;
}

static void requireStmtEnd(Parser* p) {
    if(!isImplicitEnd(&p->peek)) {
        if(match(p, TOK_NEWLINE) || match(p, TOK_SEMICOLON)) {
            advance(p);
        } else {
            error(p, "Expected token `newline` or `;`.");
        }
    }
}

//----- Recursive descent parser implementation ------

static Stmt* parseProgram(Parser* p);
static Expr* expression(Parser* p, bool tuple);

Stmt* parse(const char* fname, const char* src) {
    Parser p;
    initParser(&p, fname, src);

    Stmt* program = parseProgram(&p);
    skipNewLines(&p);

    if(!match(&p, TOK_EOF)) error(&p, "Unexpected token.");

    if(p.hadError) {
        freeStmt(program);
        return NULL;
    }

    return program;
}

Expr* parseExpression(const char* fname, const char* src) {
    Parser p;
    initParser(&p, fname, src);

    Expr* expr = expression(&p, true);
    skipNewLines(&p);

    if(!match(&p, TOK_EOF)) error(&p, "Unexpected token.");

    if(p.hadError) {
        freeExpr(expr);
        return NULL;
    }

    return expr;
}

//----- Statement parse ------
static Expr* literal(Parser* p);

typedef struct {
    LinkedList* arguments;
    LinkedList* defaults;
    bool isVararg;
} FormalArgs;

static FormalArgs formalArgs(Parser* p, TokenType open, TokenType close) {
    LinkedList *arguments = NULL, *defaults = NULL;
    bool isVararg = false;

    require(p, open);
    skipNewLines(p);

    while(match(p, TOK_IDENTIFIER)) {
        Token argument = require(p, TOK_IDENTIFIER);
        skipNewLines(p);

        if(match(p, TOK_EQUAL)) {
            rewindTo(&p->lex, &argument);
            nextToken(&p->lex, &p->peek);
            break;
        }

        arguments = addElement(arguments, newIdentifier(argument.length, argument.lexeme));
        skipNewLines(p);

        if(!match(p, close)) {
            require(p, TOK_COMMA);
            skipNewLines(p);
        }
    }

    while(match(p, TOK_IDENTIFIER)) {
        Token argument = require(p, TOK_IDENTIFIER);

        skipNewLines(p);
        require(p, TOK_EQUAL);
        skipNewLines(p);

        Expr* constant = literal(p);
        skipNewLines(p);

        if(constant && !isConstantLiteral(constant->type)) {
            error(p, "Default argument must be a constant");
        }

        arguments = addElement(arguments, newIdentifier(argument.length, argument.lexeme));
        defaults = addElement(defaults, constant);

        if(!match(p, close)) {
            require(p, TOK_COMMA);
            skipNewLines(p);
        }
    }

    if(match(p, TOK_VARARG)) {
        advance(p);
        skipNewLines(p);
        isVararg = true;
    }

    require(p, close);

    return (FormalArgs){arguments, defaults, isVararg};
}

static Stmt* parseStmt(Parser* p);

static Stmt* blockStmt(Parser* p) {
    int line = p->peek.line;
    LinkedList* stmts = NULL;

    skipNewLines(p);
    while(!isImplicitEnd(&p->peek)) {
        stmts = addElement(stmts, parseStmt(p));
        skipNewLines(p);
    }

    return newBlockStmt(line, stmts);
}

static Stmt* elifStmt(Parser* p);

static Stmt* ifBody(Parser* p, int line) {
    Expr* cond = expression(p, true);
    skipNewLines(p);

    require(p, TOK_THEN);

    Stmt* thenBody = blockStmt(p);
    Stmt* elseBody = NULL;

    if(match(p, TOK_ELIF)) {
        elseBody = elifStmt(p);
    }

    if(match(p, TOK_ELSE)) {
        advance(p);
        elseBody = blockStmt(p);
    }

    return newIfStmt(line, cond, thenBody, elseBody);
}

static Stmt* elifStmt(Parser* p) {
    int line = p->peek.line;
    require(p, TOK_ELIF);
    return ifBody(p, line);
}

static Stmt* ifStmt(Parser* p) {
    int line = p->peek.line;
    advance(p);

    Stmt* ifStmt = ifBody(p, line);
    require(p, TOK_END);

    return ifStmt;
}

static Stmt* whileStmt(Parser* p) {
    int line = p->peek.line;
    advance(p);

    Expr* cond = expression(p, true);
    skipNewLines(p);

    require(p, TOK_DO);
    Stmt* body = blockStmt(p);
    require(p, TOK_END);

    return newWhileStmt(line, cond, body);
}

static Stmt* varDecl(Parser* p) {
    int line = p->peek.line;

    bool isUnpack = false;
    LinkedList* identifiers = NULL;
    advance(p);

    do {
        Token id = require(p, TOK_IDENTIFIER);
        identifiers = addElement(identifiers, newIdentifier(id.length, id.lexeme));

        if(match(p, TOK_COMMA)) {
            advance(p);
            if(!isUnpack) isUnpack = true;
        } else {
            break;
        }
    } while(match(p, TOK_IDENTIFIER));

    Expr* init = NULL;
    if(match(p, TOK_EQUAL)) {
        advance(p);
        init = expression(p, true);
    }

    return newVarDecl(line, isUnpack, identifiers, init);
}

static Stmt* forEach(Parser* p, Stmt* var, int line) {
    if(var->as.varDecl.init != NULL) {
        error(p, "Variable declaration in foreach cannot have initializer.");
    }
    advance(p);
    skipNewLines(p);

    Expr* e = expression(p, true);
    skipNewLines(p);

    require(p, TOK_DO);

    Stmt* body = blockStmt(p);

    require(p, TOK_END);
    return newForEach(line, var, e, body);
}

static Stmt* forStmt(Parser* p) {
    int line = p->peek.line;
    advance(p);

    Stmt* init = NULL;
    if(!match(p, TOK_SEMICOLON)) {
        if(match(p, TOK_VAR)) {
            init = varDecl(p);
            if(match(p, TOK_IN)) {
                return forEach(p, init, line);
            }
        } else {
            Expr* e = expression(p, true);
            if(e != NULL) {
                init = newExprStmt(e->line, e);
            }
        }
    }

    skipNewLines(p);
    require(p, TOK_SEMICOLON);
    skipNewLines(p);

    Expr* cond = NULL;
    if(!match(p, TOK_SEMICOLON)) cond = expression(p, true);

    skipNewLines(p);
    require(p, TOK_SEMICOLON);
    skipNewLines(p);

    Expr* act = NULL;
    if(!match(p, TOK_DO)) act = expression(p, true);

    skipNewLines(p);
    require(p, TOK_DO);

    Stmt* body = blockStmt(p);

    require(p, TOK_END);
    return newForStmt(line, init, cond, act, body);
}

static Stmt* returnStmt(Parser* p) {
    int line = p->peek.line;
    advance(p);

    Expr* e = NULL;
    if(!isStatementEnd(&p->peek)) {
        e = expression(p, true);
    }

    requireStmtEnd(p);
    return newReturnStmt(line, e);
}

static Stmt* importStmt(Parser* p) {
    int line = p->peek.line;
    advance(p);

    LinkedList* modules = NULL;

    for(;;) {
        Token name = require(p, TOK_IDENTIFIER);
        modules = addElement(modules, newIdentifier(name.length, name.lexeme));
        if(!match(p, TOK_DOT)) break;
        advance(p);
    }

    Token asName = {0};
    LinkedList* importNames = NULL;

    if(match(p, TOK_FOR)) {
        advance(p);
        skipNewLines(p);

        if(match(p, TOK_MULT)) {
            Token all = require(p, TOK_MULT);
            importNames = addElement(importNames, newIdentifier(all.length, all.lexeme));
        } else {
            for(;;) {
                Token name = require(p, TOK_IDENTIFIER);
                importNames = addElement(importNames, newIdentifier(name.length, name.lexeme));
                if(!match(p, TOK_COMMA)) break;
                advance(p);
                skipNewLines(p);
            }
        }
    } else if(match(p, TOK_AS)) {
        advance(p);
        skipNewLines(p);
        asName = require(p, TOK_IDENTIFIER);
    }

    requireStmtEnd(p);
    return newImportStmt(line, modules, importNames, &asName);
}

static Stmt* tryStmt(Parser* p) {
    int line = p->peek.line;
    advance(p);

    Stmt* tryBlock = blockStmt(p);
    LinkedList* excs = NULL;
    Stmt* ensure = NULL;

    if(match(p, TOK_EXCEPT)) {
        while(match(p, TOK_EXCEPT)) {
            int excLine = p->peek.line;
            advance(p);

            Expr* cls = expression(p, true);
            Token var = require(p, TOK_IDENTIFIER);
            Stmt* block = blockStmt(p);
            excs = addElement(excs, newExceptStmt(excLine, cls, &var, block));
        }
    }

    if(match(p, TOK_ENSURE)) {
        advance(p);
        ensure = blockStmt(p);
    }

    if(!excs && !ensure) {
        error(p, "Expected except or ensure clause");
    }

    require(p, TOK_END);
    return newTryStmt(line, tryBlock, excs, ensure);
}

static Stmt* raiseStmt(Parser* p) {
    int line = p->peek.line;
    advance(p);
    Expr* exc = expression(p, true);
    requireStmtEnd(p);
    return newRaiseStmt(line, exc);
}

static Stmt* withStmt(Parser* p) {
    int line = p->peek.line;
    advance(p);

    Expr* e = expression(p, true);
    Token var = require(p, TOK_IDENTIFIER);
    Stmt* block = blockStmt(p);

    require(p, TOK_END);
    return newWithStmt(line, e, &var, block);
}

static Stmt* funcDecl(Parser* p) {
    int line = p->peek.line;
    Token fun = require(p, TOK_FUN);

    if(!match(p, TOK_IDENTIFIER)) {
        rewindTo(&p->lex, &fun);
        nextToken(&p->lex, &p->peek);
        return NULL;
    }

    Token fname = require(p, TOK_IDENTIFIER);

    FormalArgs args = formalArgs(p, TOK_LPAREN, TOK_RPAREN);
    Stmt* body = blockStmt(p);

    require(p, TOK_END);
    return newFuncDecl(line, args.isVararg, &fname, args.arguments, args.defaults, body);
}

static Stmt* nativeDecl(Parser* p) {
    int line = p->peek.line;
    advance(p);

    Token fname = require(p, TOK_IDENTIFIER);
    FormalArgs args = formalArgs(p, TOK_LPAREN, TOK_RPAREN);

    requireStmtEnd(p);
    return newNativeDecl(line, args.isVararg, &fname, args.arguments, args.defaults);
}

static Stmt* classDecl(Parser* p) {
    int line = p->peek.line;
    advance(p);

    Token clsName = require(p, TOK_IDENTIFIER);
    Expr* sup = NULL;

    if(match(p, TOK_IS)) {
        advance(p);
        sup = expression(p, true);
        skipNewLines(p);
        if(p->panic) classSynchronize(p);
    }

    skipNewLines(p);

    LinkedList* methods = NULL;
    while(!match(p, TOK_END) && !match(p, TOK_EOF)) {
        if(match(p, TOK_NAT)) {
            methods = addElement(methods, nativeDecl(p));
        } else {
            Stmt* fun = funcDecl(p);
            if(fun == NULL) {
                error(p, "Expected function or native delcaration.");
                advance(p);
            } else {
                methods = addElement(methods, fun);
            }
        }
        skipNewLines(p);
        if(p->panic) classSynchronize(p);
    }

    require(p, TOK_END);
    return newClassDecl(line, &clsName, sup, methods);
}

static Stmt* parseStmt(Parser* p) {
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
    case TOK_BEGIN:
        require(p, TOK_BEGIN);
        Stmt* block = blockStmt(p);
        require(p, TOK_END);
        return block;
    case TOK_IMPORT:
        return importStmt(p);
    case TOK_TRY:
        return tryStmt(p);
    case TOK_RAISE:
        return raiseStmt(p);
    case TOK_WITH:
        return withStmt(p);
    case TOK_CONTINUE:
        advance(p);
        requireStmtEnd(p);
        return newContinueStmt(line);
    case TOK_BREAK:
        advance(p);
        requireStmtEnd(p);
        return newBreakStmt(line);
    case TOK_CLASS:
        return classDecl(p);
    case TOK_NAT:
        return nativeDecl(p);
    case TOK_VAR: {
        Stmt* var = varDecl(p);
        requireStmtEnd(p);
        return var;
    }
    case TOK_FUN: {
        Stmt* func = funcDecl(p);
        if(func != NULL) return func;
        break;
    }
    default:
        break;
    }

    Expr* e = expression(p, true);
    requireStmtEnd(p);
    return newExprStmt(line, e);
}

static Stmt* parseProgram(Parser* p) {
    LinkedList* stmts = NULL;

    skipNewLines(p);
    while(!match(p, TOK_EOF)) {
        stmts = addElement(stmts, parseStmt(p));
        skipNewLines(p);
        if(p->panic) synchronize(p);
    }

    Token name = {0};
    return newFuncDecl(0, false, &name, NULL, NULL, newBlockStmt(0, stmts));
}

//----- Expressions parse ------

static Expr* expressionLst(Parser* p, TokenType open, TokenType close) {
    int line = p->peek.line;
    LinkedList* exprs = NULL;

    require(p, open);
    skipNewLines(p);

    while(!match(p, close)) {
        exprs = addElement(exprs, expression(p, false));
        skipNewLines(p);
        if(!match(p, TOK_COMMA)) break;
        advance(p);
        skipNewLines(p);
    }

    require(p, close);
    return newExprList(line, exprs);
}

static Expr* parseSuperLiteral(Parser* p) {
    int line = p->peek.line;
    Token name = {0};
    advance(p);

    if(match(p, TOK_DOT)) {
        advance(p);
        name = require(p, TOK_IDENTIFIER);
    }

    Expr* args = expressionLst(p, TOK_LPAREN, TOK_RPAREN);
    return newSuperLiteral(line, &name, args);
}

static Expr* parseTableLiteral(Parser* p) {
    int line = p->peek.line;
    advance(p);
    skipNewLines(p);

    LinkedList* keyVals = NULL;
    while(!match(p, TOK_RCURLY)) {
        Expr* key;
        if(match(p, TOK_DOT)) {
            advance(p);
            skipNewLines(p);
            Token id = require(p, TOK_IDENTIFIER);
            key = newStrLiteral(id.line, id.lexeme, id.length);
        } else {
            key = expression(p, false);
        }

        skipNewLines(p);
        require(p, TOK_COLON);
        skipNewLines(p);

        Expr* val = expression(p, false);
        skipNewLines(p);

        if(p->hadError) break;

        keyVals = addElement(keyVals, key);
        keyVals = addElement(keyVals, val);

        if(!match(p, TOK_RCURLY)) {
            require(p, TOK_COMMA);
            skipNewLines(p);
        }
    }

    require(p, TOK_RCURLY);
    return newTableLiteral(line, newExprList(line, keyVals));
}

static Expr* literal(Parser* p) {
    int line = p->peek.line;
    Token* tok = &p->peek;

    switch(tok->type) {
    case TOK_NUMBER: {
        Expr* e = newNumLiteral(line, strtod(tok->lexeme, NULL));
        advance(p);
        return e;
    }
    case TOK_TRUE:
    case TOK_FALSE: {
        Expr* e = newBoolLiteral(line, tok->type == TOK_TRUE);
        advance(p);
        return e;
    }
    case TOK_IDENTIFIER: {
        Expr* e = newVarLiteral(line, tok->lexeme, tok->length);
        advance(p);
        return e;
    }
    case TOK_STRING: {
        Expr* e = newStrLiteral(line, tok->lexeme + 1, tok->length - 2);
        advance(p);
        return e;
    }
    case TOK_NULL: {
        advance(p);
        return newNullLiteral(line);
    }
    case TOK_LSQUARE: {
        Expr* exprs = expressionLst(p, TOK_LSQUARE, TOK_RSQUARE);
        return newArrLiteral(line, exprs);
    }
    case TOK_SUPER: {
        return parseSuperLiteral(p);
    }
    case TOK_LCURLY: {
        return parseTableLiteral(p);
    }
    case TOK_LPAREN: {
        advance(p);
        skipNewLines(p);

        if(match(p, TOK_RPAREN)) {
            advance(p);
            return newTupleLiteral(line, newExprList(line, NULL));
        }

        Expr* e = expression(p, true);
        skipNewLines(p);
        require(p, TOK_RPAREN);
        return e;
    }
    case TOK_UNTERMINATED_STR:
        error(p, "Unterminated String.");
        advance(p);
        break;
    case TOK_ERR:
        error(p, "Invalid token.");
        advance(p);
        break;
    default:
        error(p, "Expected expression.");
        advance(p);
        break;
    }

    return NULL;
}

static Expr* postfixExpr(Parser* p) {
    Expr* lit = literal(p);

    while(match(p, TOK_LPAREN) || match(p, TOK_LCURLY) || match(p, TOK_DOT) ||
          match(p, TOK_LSQUARE)) {
        int line = p->peek.line;
        switch(p->peek.type) {
        case TOK_DOT: {
            require(p, TOK_DOT);
            Token attr = require(p, TOK_IDENTIFIER);
            lit = newAccessExpr(line, lit, attr.lexeme, attr.length);
            break;
        }
        case TOK_LCURLY: {
            Expr* table = literal(p);
            Expr* args = newExprList(line, addElement(NULL, table));
            lit = newCallExpr(line, lit, args);
            break;
        }
        case TOK_LPAREN: {
            Expr* args = expressionLst(p, TOK_LPAREN, TOK_RPAREN);
            lit = newCallExpr(line, lit, args);
            break;
        }
        case TOK_LSQUARE: {
            require(p, TOK_LSQUARE);
            skipNewLines(p);
            lit = newArrayAccExpr(line, lit, expression(p, true));
            require(p, TOK_RSQUARE);
            break;
        }
        default:
            break;
        }
    }

    return lit;
}

static Expr* anonymousFunc(Parser* p) {
    if(match(p, TOK_FUN)) {
        int line = p->peek.line;
        require(p, TOK_FUN);

        FormalArgs args = formalArgs(p, TOK_LPAREN, TOK_RPAREN);
        Stmt* body = blockStmt(p);

        require(p, TOK_END);
        return newAnonymousFunc(line, args.isVararg, args.arguments, args.defaults, body);
    }
    if(match(p, TOK_PIPE)) {
        int line = p->peek.line;

        FormalArgs args = formalArgs(p, TOK_PIPE, TOK_PIPE);

        require(p, TOK_ARROW);

        Expr* e = expression(p, false);
        Stmt* body = newBlockStmt(line, addElement(NULL, newReturnStmt(line, e)));

        return newAnonymousFunc(line, args.isVararg, args.arguments, args.defaults, body);
    }
    return postfixExpr(p);
}

static Expr* unaryExpr(Parser* p);

static Expr* powExpr(Parser* p) {
    Expr* base = anonymousFunc(p);

    while(match(p, TOK_POW)) {
        int line = p->peek.line;
        advance(p);

        Expr* exp = unaryExpr(p);
        base = newExpExpr(line, base, exp);
    }

    return base;
}

static Expr* unaryExpr(Parser* p) {
    int line = p->peek.line;
    if(match(p, TOK_BANG)) {
        advance(p);
        return newUnary(line, NOT, unaryExpr(p));
    }
    if(match(p, TOK_MINUS)) {
        advance(p);
        return newUnary(line, MINUS, unaryExpr(p));
    }
    if(match(p, TOK_HASH)) {
        advance(p);
        return newUnary(line, LENGTH, unaryExpr(p));
    }
    if(match(p, TOK_HASH_HASH)) {
        advance(p);
        return newUnary(line, STRINGOP, unaryExpr(p));
    }
    return powExpr(p);
}

static Expr* multiplicativeExpr(Parser* p) {
    Expr* l = unaryExpr(p);

    while(match(p, TOK_MULT) || match(p, TOK_DIV) || match(p, TOK_MOD)) {
        int line = p->peek.line;
        TokenType tokType = p->peek.type;
        advance(p);

        Expr* r = unaryExpr(p);
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
        default:
            break;
        }
    }

    return l;
}

static Expr* additiveExpr(Parser* p) {
    Expr* l = multiplicativeExpr(p);

    while(match(p, TOK_PLUS) || match(p, TOK_MINUS)) {
        int line = p->peek.line;
        TokenType tokType = p->peek.type;
        advance(p);

        Expr* r = multiplicativeExpr(p);
        switch(tokType) {
        case TOK_PLUS:
            l = newBinary(line, PLUS, l, r);
            break;
        case TOK_MINUS:
            l = newBinary(line, MINUS, l, r);
            break;
        default:
            break;
        }
    }

    return l;
}

static Expr* relationalExpr(Parser* p) {
    Expr* l = additiveExpr(p);

    while(match(p, TOK_GT) || match(p, TOK_GE) || match(p, TOK_LT) || match(p, TOK_LE) ||
          match(p, TOK_IS)) {
        int line = p->peek.line;
        TokenType tokType = p->peek.type;
        advance(p);

        Expr* r = additiveExpr(p);
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
        default:
            break;
        }
    }

    return l;
}

static Expr* equalityExpr(Parser* p) {
    Expr* l = relationalExpr(p);

    while(match(p, TOK_EQUAL_EQUAL) || match(p, TOK_BANG_EQ)) {
        int line = p->peek.line;
        TokenType tokType = p->peek.type;
        advance(p);

        Expr* r = relationalExpr(p);
        switch(tokType) {
        case TOK_EQUAL_EQUAL:
            l = newBinary(line, EQ, l, r);
            break;
        case TOK_BANG_EQ:
            l = newBinary(line, NEQ, l, r);
            break;
        default:
            break;
        }
    }

    return l;
}

static Expr* logicAndExpr(Parser* p) {
    Expr* l = equalityExpr(p);

    while(match(p, TOK_AND)) {
        int line = p->peek.line;
        advance(p);
        l = newBinary(line, AND, l, equalityExpr(p));
    }

    return l;
}

static Expr* logicOrExpr(Parser* p) {
    Expr* l = logicAndExpr(p);

    while(match(p, TOK_OR)) {
        int line = p->peek.line;
        advance(p);
        l = newBinary(line, OR, l, logicAndExpr(p));
    }

    return l;
}

static Expr* ternaryExpr(Parser* p) {
    int line = p->peek.line;
    Expr* expr = logicOrExpr(p);

    if(match(p, TOK_IF)) {
        advance(p);
        Expr* cond = ternaryExpr(p);
        require(p, TOK_ELSE);
        Expr* elseExpr = ternaryExpr(p);
        return newTernary(line, cond, expr, elseExpr);
    }

    return expr;
}

static Expr* tupleExpression(Parser* p, Expr* first) {
    int line = p->peek.line;
    LinkedList* exprs = addElement(NULL, first);

    while(match(p, TOK_COMMA)) {
        advance(p);
        if(match(p, TOK_RPAREN) || isStatementEnd(&p->peek)) {
            break;
        }
        exprs = addElement(exprs, ternaryExpr(p));
    }

    return newTupleLiteral(line, newExprList(line, exprs));
}

static void checkUnpackAssignement(Parser* p, Expr* lvals, TokenType assignToken) {
    foreach(n, lvals->as.list.lst) {
        if(!isLValue(((Expr*)n->elem)->type)) {
            error(p, "Left hand side of assignment must be an lvalue.");
        }
        if(assignToken != TOK_EQUAL) {
            error(p, "Unpack cannot use compund assignement.");
        }
    }
}

static Operator assignTokToOperator(TokenType t) {
    switch(t) {
    case TOK_PLUS_EQ:
        return PLUS;
    case TOK_MINUS_EQ:
        return MINUS;
    case TOK_DIV_EQ:
        return DIV;
    case TOK_MULT_EQ:
        return MULT;
    case TOK_MOD_EQ:
        return MOD;
    default:
        return -1;
    }
}

static Expr* expression(Parser* p, bool parseTuple) {
    int line = p->peek.line;
    Expr* l = ternaryExpr(p);

    if(parseTuple && match(p, TOK_COMMA)) {
        l = tupleExpression(p, l);
    }

    if(IS_ASSIGN(p->peek.type)) {
        TokenType assignToken = p->peek.type;

        if(l != NULL) {
            if(l->type == TUPLE_LIT)
                checkUnpackAssignement(p, l->as.tuple.exprs, assignToken);
            else if(!isLValue(l->type))
                error(p, "Left hand side of assignment must be an lvalue.");
        }

        advance(p);
        Expr* r = expression(p, true);

        if(IS_COMPUND_ASSIGN(assignToken)) {
            l = newCompoundAssing(line, assignTokToOperator(assignToken), l, r);
        } else {
            l = newAssign(line, l, r);
        }
    }

    return l;
}
