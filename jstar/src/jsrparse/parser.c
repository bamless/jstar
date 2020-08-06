#include "jsrparse/parser.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "jsrparse/ast.h"
#include "jsrparse/lex.h"
#include "jsrparse/vector.h"

typedef struct Parser {
    Lexer lex;
    Token peek;
    const char* path;
    const char* lineStart;
    ParseErrorCB errorCallback;
    bool panic, hadError;
} Parser;

static void initParser(Parser* p, const char* path, const char* src, ParseErrorCB errorCallback) {
    p->panic = false;
    p->hadError = false;
    p->path = path;
    p->errorCallback = errorCallback;
    initLexer(&p->lex, src);
    nextToken(&p->lex, &p->peek);
    p->lineStart = p->peek.lexeme;
}

// -----------------------------------------------------------------------------
// UTILITY FUNCTIONS
// -----------------------------------------------------------------------------

static char* strchrnul(const char* str, char c) {
    char* ret;
    return (ret = strchr(str, c)) == NULL ? strchr(str, '\0') : ret;
}

static int vstrncatf(char* buf, size_t pos, size_t maxLen, const char* fmt, va_list ap) {
    size_t bufSize = pos >= maxLen ? 0 : maxLen - pos;
    return vsnprintf(buf + pos, bufSize, fmt, ap);
}

static int strncatf(char* buf, size_t pos, size_t maxLen, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int written = vstrncatf(buf, pos, maxLen, fmt, ap);
    va_end(ap);
    return written;
}

static void error(Parser* p, const char* msg, ...) {
    if(p->panic) return;
    p->panic = p->hadError = true;

    if(p->errorCallback) {
        char errorMessage[MAX_ERR];

        // correct for escaped newlines
        const char* actualLnStart = p->lineStart;
        while((actualLnStart = strchr(actualLnStart, '\n')) && actualLnStart < p->peek.lexeme) {
            p->lineStart = ++actualLnStart;
        }

        int tokCol = p->peek.lexeme - p->lineStart;
        int lineLen = strchrnul(p->peek.lexeme, '\n') - p->lineStart;

        // print source code snippet of the token near the error
        int pos = 0;
        pos += strncatf(errorMessage, pos, MAX_ERR, "    %.*s\n", lineLen, p->lineStart);
        pos += strncatf(errorMessage, pos, MAX_ERR, "    ");
        for(int i = 0; i < tokCol; i++) {
            pos += strncatf(errorMessage, pos, MAX_ERR, " ");
        }
        pos += strncatf(errorMessage, pos, MAX_ERR, "^\n");

        // Print error message
        va_list ap;
        va_start(ap, msg);
        vstrncatf(errorMessage, pos, MAX_ERR, msg, ap);
        va_end(ap);

        p->errorCallback(p->path, p->peek.line, errorMessage);
    }
}

static bool match(Parser* p, TokenType type) {
    return p->peek.type == type;
}

static bool matchAny(Parser* p, TokenType* tokens, int count) {
    for(int i = 0; i < count; i++) {
        if(match(p, tokens[i])) return true;
    }
    return false;
}

static Token advance(Parser* p) {
    Token prev = p->peek;
    nextToken(&p->lex, &p->peek);

    if(prev.type == TOK_NEWLINE) {
        p->lineStart = p->peek.lexeme;
    }

    while(match(p, TOK_ERR) || match(p, TOK_UNTERMINATED_STR)) {
        error(p, p->peek.type == TOK_ERR ? "Invalid token." : "Unterminated string.");
        prev = p->peek;
        nextToken(&p->lex, &p->peek);
    }

    return prev;
}

static void skipNewLines(Parser* p) {
    while(match(p, TOK_NEWLINE)) {
        advance(p);
    }
}

static Token require(Parser* p, TokenType type) {
    if(match(p, type)) {
        return advance(p);
    }

    error(p, "Expected token `%s`, instead `%s` found.", TokenNames[type],
          TokenNames[p->peek.type]);
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

static bool isExpressionStart(Token* tok) {
    TokenType t = tok->type;
    return t == TOK_NUMBER || t == TOK_TRUE || t == TOK_FALSE || t == TOK_IDENTIFIER ||
           t == TOK_STRING || t == TOK_NULL || t == TOK_SUPER || t == TOK_LPAREN ||
           t == TOK_LSQUARE || t == TOK_BANG || t == TOK_MINUS || t == TOK_FUN || t == TOK_HASH ||
           t == TOK_HASH_HASH || t == TOK_LCURLY;
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

// -----------------------------------------------------------------------------
// STATEMENTS PARSE
// -----------------------------------------------------------------------------

static Expr* expression(Parser* p, bool tuple);
static Expr* literal(Parser* p);

typedef struct {
    Vector arguments;
    Vector defaults;
    bool isVararg;
} FormalArgs;

static FormalArgs formalArgs(Parser* p, TokenType open, TokenType close) {
    FormalArgs args = {0};

    require(p, open);
    skipNewLines(p);

    while(match(p, TOK_IDENTIFIER)) {
        Token argument = advance(p);
        skipNewLines(p);

        if(match(p, TOK_EQUAL)) {
            rewindTo(&p->lex, &argument);
            nextToken(&p->lex, &p->peek);
            break;
        }
        vecPush(&args.arguments, newIdentifier(argument.length, argument.lexeme));
        skipNewLines(p);

        if(!match(p, close)) {
            require(p, TOK_COMMA);
            skipNewLines(p);
        }
    }

    while(match(p, TOK_IDENTIFIER)) {
        Token argument = advance(p);

        skipNewLines(p);
        require(p, TOK_EQUAL);
        skipNewLines(p);

        Expr* constant = literal(p);
        skipNewLines(p);

        if(constant && !isConstantLiteral(constant->type)) {
            error(p, "Default argument must be a constant");
        }

        vecPush(&args.arguments, newIdentifier(argument.length, argument.lexeme));
        vecPush(&args.defaults, constant);

        if(!match(p, close)) {
            require(p, TOK_COMMA);
            skipNewLines(p);
        }
    }

    if(match(p, TOK_VARARG)) {
        advance(p);
        skipNewLines(p);
        args.isVararg = true;
    }

    require(p, close);
    return args;
}

static Stmt* parseStmt(Parser* p);

static Stmt* blockStmt(Parser* p) {
    int line = p->peek.line;
    skipNewLines(p);

    Vector stmts = vecNew();
    while(!isImplicitEnd(&p->peek)) {
        vecPush(&stmts, parseStmt(p));
        skipNewLines(p);
    }

    return newBlockStmt(line, &stmts);
}

static Stmt* ifBody(Parser* p, int line) {
    Expr* cond = expression(p, true);
    skipNewLines(p);

    require(p, TOK_THEN);

    Stmt* thenBody = blockStmt(p);
    Stmt* elseBody = NULL;

    if(match(p, TOK_ELIF)) {
        int line = p->peek.line;
        advance(p);
        elseBody = ifBody(p, line);
    } else if(match(p, TOK_ELSE)) {
        advance(p);
        elseBody = blockStmt(p);
    }

    return newIfStmt(line, cond, thenBody, elseBody);
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
    advance(p);

    bool isUnpack = false;
    Vector identifiers = vecNew();

    do {
        Token id = require(p, TOK_IDENTIFIER);
        vecPush(&identifiers, newIdentifier(id.length, id.lexeme));

        if(match(p, TOK_COMMA)) {
            advance(p);
            isUnpack = true;
        } else {
            break;
        }
    } while(match(p, TOK_IDENTIFIER));

    Expr* init = NULL;
    if(match(p, TOK_EQUAL)) {
        advance(p);
        init = expression(p, true);
    }

    return newVarDecl(line, isUnpack, &identifiers, init);
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

    Vector modules = vecNew();

    for(;;) {
        Token name = require(p, TOK_IDENTIFIER);
        vecPush(&modules, newIdentifier(name.length, name.lexeme));
        if(!match(p, TOK_DOT)) break;
        advance(p);
    }

    Token asName = {0};
    Vector importNames = vecNew();

    if(match(p, TOK_FOR)) {
        advance(p);
        skipNewLines(p);

        if(match(p, TOK_MULT)) {
            Token all = advance(p);
            vecPush(&importNames, newIdentifier(all.length, all.lexeme));
        } else {
            for(;;) {
                Token name = require(p, TOK_IDENTIFIER);
                vecPush(&importNames, newIdentifier(name.length, name.lexeme));
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
    return newImportStmt(line, &modules, &importNames, &asName);
}

static Stmt* tryStmt(Parser* p) {
    int line = p->peek.line;
    advance(p);

    Stmt* tryBlock = blockStmt(p);
    Vector excs = vecNew();
    Stmt* ensure = NULL;

    while(match(p, TOK_EXCEPT)) {
        int excLine = p->peek.line;
        advance(p);

        Expr* cls = expression(p, true);
        Token var = require(p, TOK_IDENTIFIER);
        Stmt* block = blockStmt(p);
        vecPush(&excs, newExceptStmt(excLine, cls, &var, block));
    }

    if(match(p, TOK_ENSURE)) {
        advance(p);
        ensure = blockStmt(p);
    }

    if(vecEmpty(&excs) && !ensure) {
        error(p, "Expected except or ensure clause");
    }

    require(p, TOK_END);
    return newTryStmt(line, tryBlock, &excs, ensure);
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
    Token fun = advance(p);

    if(!match(p, TOK_IDENTIFIER)) {
        rewindTo(&p->lex, &fun);
        nextToken(&p->lex, &p->peek);
        return NULL;
    }

    Token funcName = require(p, TOK_IDENTIFIER);
    FormalArgs args = formalArgs(p, TOK_LPAREN, TOK_RPAREN);
    Stmt* body = blockStmt(p);
    require(p, TOK_END);

    return newFuncDecl(line, &funcName, &args.arguments, &args.defaults, args.isVararg, body);
}

static Stmt* nativeDecl(Parser* p) {
    int line = p->peek.line;
    advance(p);

    Token funcName = require(p, TOK_IDENTIFIER);
    FormalArgs args = formalArgs(p, TOK_LPAREN, TOK_RPAREN);
    requireStmtEnd(p);

    return newNativeDecl(line, &funcName, &args.arguments, &args.defaults, args.isVararg);
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

    Vector methods = vecNew();
    while(!match(p, TOK_END) && !match(p, TOK_EOF)) {
        if(match(p, TOK_NAT)) {
            vecPush(&methods, nativeDecl(p));
        } else {
            Stmt* fun = funcDecl(p);
            if(fun != NULL) {
                vecPush(&methods, fun);
            } else {
                error(p, "Expected function or native delcaration.");
                advance(p);
            }
        }
        skipNewLines(p);
        if(p->panic) classSynchronize(p);
    }

    require(p, TOK_END);
    return newClassDecl(line, &clsName, sup, &methods);
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
    case TOK_IMPORT:
        return importStmt(p);
    case TOK_TRY:
        return tryStmt(p);
    case TOK_RAISE:
        return raiseStmt(p);
    case TOK_WITH:
        return withStmt(p);
    case TOK_CLASS:
        return classDecl(p);
    case TOK_NAT:
        return nativeDecl(p);
    case TOK_CONTINUE:
        advance(p);
        requireStmtEnd(p);
        return newContinueStmt(line);
    case TOK_BREAK:
        advance(p);
        requireStmtEnd(p);
        return newBreakStmt(line);
    case TOK_BEGIN: {
        require(p, TOK_BEGIN);
        Stmt* block = blockStmt(p);
        require(p, TOK_END);
        return block;
    }
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
    skipNewLines(p);

    Vector stmts = vecNew();
    while(!match(p, TOK_EOF)) {
        vecPush(&stmts, parseStmt(p));
        skipNewLines(p);
        if(p->panic) synchronize(p);
    }

    return newFuncDecl(0, &(Token){0}, &(Vector){0}, &(Vector){0}, false, newBlockStmt(0, &stmts));
}

// -----------------------------------------------------------------------------
// EXPRESSIONS PARSE
// -----------------------------------------------------------------------------

static Expr* expressionLst(Parser* p, TokenType open, TokenType close) {
    int line = p->peek.line;
    require(p, open);
    skipNewLines(p);

    Vector exprs = vecNew();
    while(!match(p, close)) {
        vecPush(&exprs, expression(p, false));
        skipNewLines(p);
        if(!match(p, TOK_COMMA)) break;
        advance(p);
        skipNewLines(p);
    }

    require(p, close);
    return newExprList(line, &exprs);
}

static Expr* parseTableLiteral(Parser* p) {
    int line = p->peek.line;
    advance(p);
    skipNewLines(p);

    Vector keyVals = vecNew();
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

        vecPush(&keyVals, key);
        vecPush(&keyVals, val);

        if(!match(p, TOK_RCURLY)) {
            require(p, TOK_COMMA);
            skipNewLines(p);
        }
    }

    require(p, TOK_RCURLY);
    return newTableLiteral(line, newExprList(line, &keyVals));
}

static Expr* parseSuperLiteral(Parser* p) {
    int line = p->peek.line;
    advance(p);

    Token name = {0};
    Expr* args = NULL;

    if(match(p, TOK_DOT)) {
        advance(p);
        name = require(p, TOK_IDENTIFIER);
    }

    if(match(p, TOK_LPAREN)) {
        args = expressionLst(p, TOK_LPAREN, TOK_RPAREN);
    } else if(match(p, TOK_LCURLY)) {
        Vector tableCallArgs = vecNew();
        vecPush(&tableCallArgs, parseTableLiteral(p));
        args = newExprList(line, &tableCallArgs);
    }

    return newSuperLiteral(line, &name, args);
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
    case TOK_LSQUARE: {
        Expr* exprs = expressionLst(p, TOK_LSQUARE, TOK_RSQUARE);
        return newArrLiteral(line, exprs);
    }
    case TOK_LPAREN: {
        advance(p);
        skipNewLines(p);

        if(match(p, TOK_RPAREN)) {
            advance(p);
            return newTupleLiteral(line, newExprList(line, &(Vector){0}));
        }

        Expr* e = expression(p, true);
        skipNewLines(p);
        require(p, TOK_RPAREN);
        return e;
    }
    case TOK_TRUE:
        return advance(p), newBoolLiteral(line, true);
    case TOK_FALSE:
        return advance(p), newBoolLiteral(line, false);
    case TOK_NULL:
        return advance(p), newNullLiteral(line);
    case TOK_SUPER:
        return parseSuperLiteral(p);
    case TOK_LCURLY:
        return parseTableLiteral(p);
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

    TokenType tokens[] = {TOK_LPAREN, TOK_LCURLY, TOK_DOT, TOK_LSQUARE};
    while(matchAny(p, tokens, sizeof(tokens) / sizeof(TokenType))) {
        int line = p->peek.line;
        switch(p->peek.type) {
        case TOK_DOT: {
            advance(p);
            Token attr = require(p, TOK_IDENTIFIER);
            lit = newAccessExpr(line, lit, attr.lexeme, attr.length);
            break;
        }
        case TOK_LCURLY: {
            Vector tableCallArgs = vecNew();
            vecPush(&tableCallArgs, parseTableLiteral(p));
            Expr* args = newExprList(line, &tableCallArgs);
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

        return newAnonymousFunc(line, &args.arguments, &args.defaults, args.isVararg, body);
    }
    if(match(p, TOK_PIPE)) {
        int line = p->peek.line;
        FormalArgs args = formalArgs(p, TOK_PIPE, TOK_PIPE);

        require(p, TOK_ARROW);

        Expr* e = expression(p, false);
        Vector anonFuncStmts = vecNew();
        vecPush(&anonFuncStmts, newReturnStmt(line, e));
        Stmt* body = newBlockStmt(line, &anonFuncStmts);

        return newAnonymousFunc(line, &args.arguments, &args.defaults, args.isVararg, body);
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
    TokenType tokens[] = {TOK_BANG, TOK_MINUS, TOK_HASH, TOK_HASH_HASH};

    if(matchAny(p, tokens, sizeof(tokens) / sizeof(TokenType))) {
        Token op = advance(p);
        return newUnary(op.line, op.type, unaryExpr(p));
    }

    return powExpr(p);
}

static Expr* parseBinary(Parser* p, TokenType* tokens, int count, Expr* (*operand)(Parser*)) {
    Expr* l = (*operand)(p);

    while(matchAny(p, tokens, count)) {
        Token op = advance(p);
        Expr* r = (*operand)(p);
        l = newBinary(op.line, op.type, l, r);
    }

    return l;
}

static Expr* multiplicativeExpr(Parser* p) {
    TokenType tokens[] = {TOK_MULT, TOK_DIV, TOK_MOD};
    return parseBinary(p, tokens, sizeof(tokens) / sizeof(TokenType), unaryExpr);
}

static Expr* additiveExpr(Parser* p) {
    TokenType tokens[] = {TOK_PLUS, TOK_MINUS};
    return parseBinary(p, tokens, sizeof(tokens) / sizeof(TokenType), multiplicativeExpr);
}

static Expr* relationalExpr(Parser* p) {
    TokenType tokens[] = {TOK_GT, TOK_GE, TOK_LT, TOK_LE, TOK_IS};
    return parseBinary(p, tokens, sizeof(tokens) / sizeof(TokenType), additiveExpr);
}

static Expr* equalityExpr(Parser* p) {
    TokenType tokens[] = {TOK_EQUAL_EQUAL, TOK_BANG_EQ};
    return parseBinary(p, tokens, sizeof(tokens) / sizeof(TokenType), relationalExpr);
}

static Expr* logicAndExpr(Parser* p) {
    TokenType tokens[] = {TOK_AND};
    return parseBinary(p, tokens, sizeof(tokens) / sizeof(TokenType), equalityExpr);
}

static Expr* logicOrExpr(Parser* p) {
    TokenType tokens[] = {TOK_OR};
    return parseBinary(p, tokens, sizeof(tokens) / sizeof(TokenType), logicAndExpr);
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

    Vector exprs = vecNew();
    vecPush(&exprs, first);

    while(match(p, TOK_COMMA)) {
        advance(p);
        if(!isExpressionStart(&p->peek)) break;
        vecPush(&exprs, ternaryExpr(p));
    }

    return newTupleLiteral(line, newExprList(line, &exprs));
}

static void checkUnpackAssignement(Parser* p, Expr* lvals, TokenType assignToken) {
    vecForeach(Expr(**it), lvals->as.list) {
        Expr* expr = *it;
        if(!isLValue(expr->type)) {
            error(p, "Left hand side of assignment must be an lvalue.");
        }
        if(assignToken != TOK_EQUAL) {
            error(p, "Unpack cannot use compound assignement.");
        }
    }
}

static TokenType compundToAssign(TokenType t) {
    switch(t) {
    case TOK_PLUS_EQ:
        return TOK_PLUS;
    case TOK_MINUS_EQ:
        return TOK_MINUS;
    case TOK_DIV_EQ:
        return TOK_DIV;
    case TOK_MULT_EQ:
        return TOK_MULT;
    case TOK_MOD_EQ:
        return TOK_MOD;
    default:
        UNREACHABLE();
        return -1;
    }
}

static Expr* expression(Parser* p, bool parseTuple) {
    Expr* l = ternaryExpr(p);

    if(l && parseTuple && match(p, TOK_COMMA)) {
        l = tupleExpression(p, l);
    }

    if(IS_ASSIGN(p->peek.type)) {
        if(l && l->type == TUPLE_LIT) {
            checkUnpackAssignement(p, l->as.tuple.exprs, p->peek.type);
        } else if(l && !isLValue(l->type)) {
            error(p, "Left hand side of assignment must be an lvalue.");
        }

        Token assign = advance(p);
        Expr* r = expression(p, true);

        if(IS_COMPUND_ASSIGN(assign.type)) {
            l = newCompoundAssing(assign.line, compundToAssign(assign.type), l, r);
        } else {
            l = newAssign(assign.line, l, r);
        }
    }

    return l;
}

// -----------------------------------------------------------------------------
// API
// -----------------------------------------------------------------------------

Stmt* parse(const char* path, const char* src, ParseErrorCB errorCallback) {
    Parser p;
    initParser(&p, path, src, errorCallback);

    Stmt* program = parseProgram(&p);
    skipNewLines(&p);

    if(!match(&p, TOK_EOF)) error(&p, "Unexpected token.");

    if(p.hadError) {
        freeStmt(program);
        return NULL;
    }

    return program;
}

Expr* parseExpression(const char* path, const char* src, ParseErrorCB errorCallback) {
    Parser p;
    initParser(&p, path, src, errorCallback);

    Expr* expr = expression(&p, true);
    skipNewLines(&p);

    if(!match(&p, TOK_EOF)) error(&p, "Unexpected token.");

    if(p.hadError) {
        freeExpr(expr);
        return NULL;
    }

    return expr;
}
