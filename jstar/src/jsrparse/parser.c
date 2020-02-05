#include "parser.h"
#include "linkedlist.h"
#include "token.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct Parser {
    Lexer lex;
    Token peek;

    const char *fname;
    TokenType prevType;
    const char *lnStart;

    bool panic, hadError;
} Parser;

static void initParser(Parser *p, const char *fname, const char *src) {
    p->panic = false;
    p->hadError = false;
    p->fname = fname;
    p->prevType = -1;
    initLexer(&p->lex, src);
    nextToken(&p->lex, &p->peek);
    p->lnStart = p->peek.lexeme;
}

//----- Utility functions ------

static char *strchrnul(const char *str, char c) {
    char *ret;
    return (ret = strchr(str, c)) == NULL ? strchr(str, '\0') : ret;
}

static void error(Parser *p, const char *msg) {
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
        fprintf(stderr, "%s\n", msg);
    }
}

static bool match(Parser *p, TokenType type) {
    return p->peek.type == type;
}

static void advance(Parser *p) {
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

static void skipNewLines(Parser *p) {
    while(p->peek.type == TOK_NEWLINE) {
        advance(p);
    }
}

static Token require(Parser *p, TokenType type) {
    if(match(p, type)) {
        Token t = p->peek;
        advance(p);
        return t;
    }

    char message[512];
    const char *expected = tokNames[type];
    const char *found = tokNames[p->peek.type];
    snprintf(message, sizeof(message), "Expected token `%s`, instead `%s` found.", expected, found);
    error(p, message);

    return (Token) {0, NULL, 0, 0};
}

static void requireStmtEnd(Parser *p) {
    if(!IS_IMPLICIT_END(p->peek.type)) {                    
        if(match(p, TOK_NEWLINE) || match(p, TOK_SEMICOLON)) {
            advance(p);                                     
        } else {                                                
            error(p, "Expected token `newline` or `;`.");
        }   
    }         
}

static void synchronize(Parser *p) {
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

static void classSynchronize(Parser *p) {
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

//----- Recursive descent parser implementation ------

static Stmt *parseProgram(Parser *p);
static Expr *expression(Parser *p, bool tuple);

Stmt *parse(const char *fname, const char *src) {
    Parser p;
    initParser(&p, fname, src);

    Stmt *program = parseProgram(&p);
    skipNewLines(&p);

    if(!match(&p, TOK_EOF)) error(&p, "Unexpected token.");

    if(p.hadError) {
        freeStmt(program);
        return NULL;
    }

    return program;
}

Expr *parseExpression(const char *fname, const char *src) {
    Parser p;
    initParser(&p, fname, src);

    Expr *expr = expression(&p, true);
    skipNewLines(&p);

    if(!match(&p, TOK_EOF)) error(&p, "Unexpected token.");

    if(p.hadError) {
        freeExpr(expr);
        return NULL;
    }

    return expr;
}

//----- Statement parse ------
static Expr *literal(Parser *p);

static void formalArgs(Parser *p, LinkedList **args, LinkedList **defArgs, 
    bool *vararg, TokenType open, TokenType close) 
{
    require(p, open);
    skipNewLines(p);

    Token arg = {0};

    while((*args == NULL || match(p, TOK_COMMA)) && !match(p, close)) {
        if(*args != NULL) {
            advance(p);
            skipNewLines(p);
        }

        if(match(p, TOK_VARARG)) {
            advance(p);
            skipNewLines(p);
            require(p, close);
            *vararg = true;
            return;
        }

        arg = require(p, TOK_IDENTIFIER);
        skipNewLines(p);

        if(match(p, TOK_EQUAL)) {
            break;
        }

        *args = addElement(*args, newIdentifier(arg.length, arg.lexeme));
    }

    skipNewLines(p);

    while((*defArgs == NULL || match(p, TOK_COMMA)) && !match(p, close)) {
        if(*defArgs != NULL) {
            if(match(p, TOK_COMMA)) {
                advance(p);
                skipNewLines(p);
            }

            if(match(p, TOK_VARARG)) {
                advance(p);
                skipNewLines(p);
                require(p, close);
                *vararg = true;
                return;
            }

            arg = require(p, TOK_IDENTIFIER);
        }

        require(p, TOK_EQUAL);
        skipNewLines(p);

        Expr *c = literal(p);
        skipNewLines(p);

        if(c != NULL && !IS_CONSTANT_LITERAL(c->type)) {
            error(p, "Default argument must be a constant");
        }

        *args = addElement(*args, newIdentifier(arg.length, arg.lexeme));
        *defArgs = addElement(*defArgs, c);
    }

    require(p, close);
}

static Stmt *parseStmt(Parser *p);

static Stmt *blockStmt(Parser *p) {
    int line = p->peek.line;
    LinkedList *stmts = NULL;
    
    skipNewLines(p);
    while(!IS_IMPLICIT_END(p->peek.type)) {
        stmts = addElement(stmts, parseStmt(p));
        skipNewLines(p);
    }

    return newBlockStmt(line, stmts);
}


static Stmt *elifStmt(Parser *p);

static Stmt *ifBody(Parser *p, int line) {
    Expr *cond = expression(p, true);
    skipNewLines(p);

    require(p, TOK_THEN);

    Stmt *thenBody = blockStmt(p);
    Stmt *elseBody = NULL;

    if(match(p, TOK_ELIF)) {
        elseBody = elifStmt(p);
    }

    if(match(p, TOK_ELSE)) {
        advance(p);
        elseBody = blockStmt(p);
    }

    return newIfStmt(line, cond, thenBody, elseBody);
}

static Stmt *elifStmt(Parser *p) {
    int line = p->peek.line;
    require(p, TOK_ELIF);
    return ifBody(p, line);
}

static Stmt *ifStmt(Parser *p) {
    int line = p->peek.line;
    advance(p);

    Stmt *ifStmt = ifBody(p, line);
    require(p, TOK_END);

    return ifStmt;
}

static Stmt *whileStmt(Parser *p) {
    int line = p->peek.line;
    advance(p);

    Expr *cond = expression(p, true);
    skipNewLines(p);

    require(p, TOK_DO);
    Stmt *body = blockStmt(p);
    require(p, TOK_END);

    return newWhileStmt(line, cond, body);
}

static Stmt *varDecl(Parser *p) {
    int line = p->peek.line;
    
    bool isUnpack = false;
    LinkedList *identifiers = NULL;

    advance(p);

    do {
        Token id = require(p, TOK_IDENTIFIER);
        identifiers = addElement(identifiers, newIdentifier(id.length, id.lexeme));

        if(match(p, TOK_COMMA)) {
            advance(p);
            if(!isUnpack) isUnpack = true;
        }
    } while(match(p, TOK_IDENTIFIER));


    Expr *init = NULL;
    if(match(p, TOK_EQUAL)) {
        advance(p);
        init = expression(p, true);
    }

    return newVarDecl(line, isUnpack, identifiers, init);
}

static Stmt *forStmt(Parser *p) {
    int line = p->peek.line;
    advance(p);

    Stmt *init = NULL;
    if(!match(p, TOK_SEMICOLON)) {
        if(match(p, TOK_VAR)) {
            init = varDecl(p);

            if(match(p, TOK_IN)) {
                if(init->as.varDecl.init != NULL) {
                    error(p, "Variable declaration in foreach cannot have initializer.");
                }

                advance(p);
                skipNewLines(p);

                Expr *e = expression(p, true);
                skipNewLines(p);

                require(p, TOK_DO);

                Stmt *body = blockStmt(p);

                require(p, TOK_END);

                return newForEach(line, init, e, body);
            }
        } else {
            Expr *e = expression(p, true);
            if(e != NULL) {
                init = newExprStmt(e->line, e);
            }
        }
    }

    skipNewLines(p);
    require(p, TOK_SEMICOLON);
    skipNewLines(p);

    Expr *cond = NULL;
    if(!match(p, TOK_SEMICOLON)) cond = expression(p, true);

    skipNewLines(p);
    require(p, TOK_SEMICOLON);
    skipNewLines(p);

    Expr *act = NULL;
    if(!match(p, TOK_DO)) act = expression(p, true);

    skipNewLines(p);
    require(p, TOK_DO);

    Stmt *body = blockStmt(p);

    require(p, TOK_END);

    return newForStmt(line, init, cond, act, body);
}

static Stmt *returnStmt(Parser *p) {
    int line = p->peek.line;
    advance(p);

    Expr *e = NULL;
    if(!IS_STATEMENT_END(p->peek.type)) {
        e = expression(p, true);
    }

    requireStmtEnd(p);

    return newReturnStmt(line, e);
}

static Stmt *importStmt(Parser *p) {
    int line = p->peek.line;
    advance(p);

    LinkedList *modules = NULL;

    for(;;) {
        Token name = require(p, TOK_IDENTIFIER);
        modules = addElement(modules, newIdentifier(name.length, name.lexeme));
        if(!match(p, TOK_DOT)) break;
        advance(p);
    }

    Token as = {0};
    LinkedList *importNames = NULL;

    if(match(p, TOK_FOR)) {
        advance(p);
        skipNewLines(p);

        if(match(p, TOK_MULT)) {
            Token all = require(p, TOK_MULT);
            importNames = addElement(importNames, newIdentifier(all.length, all.lexeme));
        } else {
            for(;;) {
                skipNewLines(p);

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
        as = require(p, TOK_IDENTIFIER);
    }

    requireStmtEnd(p);

    return newImportStmt(line, modules, importNames, as.lexeme, as.length);
}

static Stmt *tryStmt(Parser *p) {
    int line = p->peek.line;
    advance(p);

    Stmt *tryBlock = blockStmt(p);
    LinkedList *excs = NULL;
    Stmt *ensure = NULL;

    if(match(p, TOK_EXCEPT)) {
        while(match(p, TOK_EXCEPT)) {
            int excLine = p->peek.line;
            advance(p);

            Expr *cls = expression(p, true);
            Token exc = require(p, TOK_IDENTIFIER);
            Stmt *block = blockStmt(p);

            excs = addElement(excs, newExceptStmt(excLine, cls, exc.length, exc.lexeme, block));
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

static Stmt *raiseStmt(Parser *p) {
    int line = p->peek.line;
    advance(p);

    Expr *exc = expression(p, true);
    
    requireStmtEnd(p);

    return newRaiseStmt(line, exc);
}

static Stmt *withStmt(Parser *p) {
    int line = p->peek.line;
    advance(p);

    Expr *e = expression(p, true);

    Token var = require(p, TOK_IDENTIFIER);

    Stmt *block = blockStmt(p);
    require(p, TOK_END);

    return newWithStmt(line, e, var.length, var.lexeme, block);
}

static Stmt *funcDecl(Parser *p) {
    int line = p->peek.line;

    Token fun = require(p, TOK_FUN);

    if(!match(p, TOK_IDENTIFIER)) {
        rewindTo(&p->lex, &fun);
        nextToken(&p->lex, &p->peek);
        return NULL;
    }

    Token fname = require(p, TOK_IDENTIFIER);

    bool vararg = false;
    LinkedList *args = NULL, *defArgs = NULL;
    formalArgs(p, &args, &defArgs, &vararg, TOK_LPAREN, TOK_RPAREN);

    Stmt *body = blockStmt(p);

    require(p, TOK_END);

    return newFuncDecl(line, vararg, fname.length, fname.lexeme, args, defArgs, body);
}

static Stmt *nativeDecl(Parser *p) {
    int line = p->peek.line;
    advance(p);

    Token fname = require(p, TOK_IDENTIFIER);

    bool vararg = false;
    LinkedList *args = NULL, *defArgs = NULL;
    formalArgs(p, &args, &defArgs, &vararg, TOK_LPAREN, TOK_RPAREN);

    requireStmtEnd(p);

    return newNativeDecl(line, vararg, fname.length, fname.lexeme, args, defArgs);
}

static Stmt *classDecl(Parser *p) {
    int line = p->peek.line;
    advance(p);

    Token cls = require(p, TOK_IDENTIFIER);

    Expr *sup = NULL;
    if(match(p, TOK_IS)) {
        advance(p);
        
        sup = expression(p, true);
        skipNewLines(p);
        
        if(p->panic) classSynchronize(p);
    }

    skipNewLines(p);

    LinkedList *methods = NULL;
    while(!match(p, TOK_END) && !match(p, TOK_EOF)) {
        if(match(p, TOK_NAT)) {
            methods = addElement(methods, nativeDecl(p));
        } else {
            Stmt *fun = funcDecl(p);
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

    return newClassDecl(line, cls.length, cls.lexeme, sup, methods);
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
    case TOK_BEGIN:
        require(p, TOK_BEGIN);
        Stmt *block = blockStmt(p);
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
        Stmt *var = varDecl(p);
        requireStmtEnd(p);
        return var;
    }
    case TOK_FUN: {
        Stmt *func = funcDecl(p);
        if(func != NULL) return func;
        break;
    }
    default: break;
    }

    Expr *e = expression(p, true);

    requireStmtEnd(p);
    
    return newExprStmt(line, e);
}

static Stmt *parseProgram(Parser *p) {
    LinkedList *stmts = NULL;

    skipNewLines(p);
    while(!match(p, TOK_EOF)) {
        stmts = addElement(stmts, parseStmt(p));
        skipNewLines(p);

        if(p->panic) synchronize(p);
    }

    return newFuncDecl(0, false, 0, NULL, NULL, NULL, newBlockStmt(0, stmts));
}

//----- Expressions parse ------

static LinkedList *expressionLst(Parser *p, TokenType open, TokenType close) {
    LinkedList *exprs = NULL;

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
    return exprs;
}

static Expr *literal(Parser *p) {
    int line = p->peek.line;
    switch(p->peek.type) {
    case TOK_NUMBER: {
        const char *end = p->peek.lexeme + p->peek.length;
        double num = strtod(p->peek.lexeme, (char **)&end);
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
        Expr *e = newStrLiteral(line, p->peek.lexeme + 1, p->peek.length - 2);
        advance(p);
        return e;
    }
    case TOK_COMMAND: {
        Expr *e = newCmdLiteral(line, p->peek.lexeme + 1, p->peek.length - 2);
        advance(p);
        return e;
    }
    case TOK_NULL: {
        advance(p);
        return newNullLiteral(line);
    }
    case TOK_LPAREN: {
        advance(p);
        skipNewLines(p);

        if(match(p, TOK_RPAREN)) {
            advance(p);
            return newTupleLiteral(line, newExprList(line, NULL));
        }
     
        Expr *e = expression(p, true);
        skipNewLines(p);
     
        require(p, TOK_RPAREN);
        return e;
    }
    case TOK_LSQUARE: {
        LinkedList *exprs = expressionLst(p, TOK_LSQUARE, TOK_RSQUARE);
        return newArrLiteral(line, newExprList(line, exprs));
    }
    case TOK_SUPER: {
        const char *name = NULL;
        size_t nameLen = 0;
        
        advance(p);

        if(match(p, TOK_DOT)) {
            advance(p);
            Token id = require(p, TOK_IDENTIFIER);
            name = id.lexeme;
            nameLen = id.length;
        }

        LinkedList *args = expressionLst(p, TOK_LPAREN, TOK_RPAREN);
        return newSuperLiteral(line, name, nameLen, newExprList(line, args));
    }
    case TOK_LCURLY: {
        advance(p);
        skipNewLines(p);

        LinkedList *keyVals = NULL;
        while(!match(p, TOK_RCURLY)) {
            Expr *key;
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

            Expr *val = expression(p, false);
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

static Expr *postfixExpr(Parser *p) {
    Expr *lit = literal(p);

    while(match(p, TOK_LPAREN) || match(p, TOK_LCURLY) || match(p, TOK_DOT) || 
          match(p, TOK_LSQUARE))
    {
        int line = p->peek.line;
        switch(p->peek.type) {
        case TOK_DOT: {
            require(p, TOK_DOT);
            Token attr = require(p, TOK_IDENTIFIER);
            lit = newAccessExpr(line, lit, attr.lexeme, attr.length);
            break;
        }
        case TOK_LCURLY: {
            Expr *table = literal(p);
            lit = newCallExpr(line, lit, addElement(NULL, table));
            break;
        }
        case TOK_LPAREN: {
            LinkedList *args = expressionLst(p, TOK_LPAREN, TOK_RPAREN);
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

static Expr *anonymousFunc(Parser *p) {
    if(match(p, TOK_FUN)) {
        int line = p->peek.line;
        require(p, TOK_FUN);
        
        bool vararg = false;
        LinkedList *args = NULL, *defArgs = NULL;
        formalArgs(p, &args, &defArgs, &vararg, TOK_LPAREN, TOK_RPAREN);

        Stmt *body = blockStmt(p);

        require(p, TOK_END);

        return newAnonymousFunc(line, vararg, args, defArgs, body);
    }
    if(match(p, TOK_PIPE)) {
        int line = p->peek.line;

        bool vararg = false;
        LinkedList *args = NULL, *defArgs = NULL;
        formalArgs(p, &args, &defArgs, &vararg, TOK_PIPE, TOK_PIPE);

        require(p, TOK_ARROW);

        Expr *e = expression(p, false);
        Stmt *body = newBlockStmt(line, addElement(NULL, newReturnStmt(line, e)));

        return newAnonymousFunc(line, vararg, args, defArgs, body);
    }
    return postfixExpr(p);
}

static Expr *unaryExpr(Parser *p);

static Expr *powExpr(Parser *p) {
    Expr *base = anonymousFunc(p);

    while(match(p, TOK_POW)) {
        int line = p->peek.line;
        advance(p);

        Expr *exp = unaryExpr(p);
        base = newExpExpr(line, base, exp);
    }

    return base;
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
        default:
            break;
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
        default:
            break;
        }
    }

    return l;
}

static Expr *relationalExpr(Parser *p) {
    Expr *l = additiveExpr(p);

    while(match(p, TOK_GT) || match(p, TOK_GE) || match(p, TOK_LT) || match(p, TOK_LE) ||
          match(p, TOK_IS)) 
    {
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
        default:
            break;
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
        default:
            break;
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

static Expr *ternaryExpr(Parser *p) {
    int line = p->peek.line;
    Expr *expr = logicOrExpr(p);

    if(match(p, TOK_IF)) {
        advance(p);
        Expr *cond = ternaryExpr(p);

        require(p, TOK_ELSE);

        Expr *elseExpr = ternaryExpr(p);

        return newTernary(line, cond, expr, elseExpr);
    }

    return expr;
}

static Operator tokenToOperator(TokenType t) {
    switch(t) {
    case TOK_PLUS:
        return PLUS;
    case TOK_MINUS:
        return MINUS;
    case TOK_DIV:
        return DIV;
    case TOK_MULT:
        return MULT;
    case TOK_MOD:
        return MOD;
    default:
        return -1;
    }
}

static void checkUnpackAssignement(Parser *p, LinkedList *lst, TokenType assignToken) {
    foreach(n, lst) {
        if(!IS_LVALUE(((Expr *)n->elem)->type)) {
            error(p, "Left hand side of assignment must be an lvalue.");
        }
        if(assignToken != TOK_EQUAL) {
            error(p, "Unpack cannot use compund assignement.");
        }
    }
}

static Expr *expression(Parser *p, bool parseTuple) {
    int line = p->peek.line;
    Expr *l = ternaryExpr(p);

    if(parseTuple && match(p, TOK_COMMA)) {
        LinkedList *exprs = addElement(NULL, l);

        while(match(p, TOK_COMMA)) {
            advance(p);
            if(!IS_EXPR_START(p->peek.type)) break;
            exprs = addElement(exprs, ternaryExpr(p));
        }

        l = newTupleLiteral(line, newExprList(line, exprs));
    }

    if(IS_ASSIGN(p->peek.type)) {
        TokenType assignToken = p->peek.type;

        if(l != NULL) {
            if(l->type == TUPLE_LIT)
                checkUnpackAssignement(p, l->as.tuple.exprs->as.list.lst, assignToken);
            else if(!IS_LVALUE(l->type))
                error(p, "Left hand side of assignment must be an lvalue.");
        }

        advance(p);
        Expr *r = expression(p, true);

        // check if we're parsing a compund assginment
        if(IS_COMPUND_ASSIGN(assignToken)) {
            l = newCompoundAssing(line, tokenToOperator(COMPUND_ASS_TO_OP(assignToken)), l, r);
        } else {
            l = newAssign(line, l, r);
        }
    }

    return l;
}
