#include "parse/parser.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "array.h"
#include "conf.h"
#include "parse/ast.h"
#include "parse/lex.h"
#include "profiler.h"

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))
#define MAX_ERR_SIZE    512
#define CONSTRUCT_ID    "@construct"

typedef struct Function {
    bool isGenerator, isCtor;
    struct Function* parent;
} Function;

typedef struct Parser {
    JStarLex lex;
    JStarTok peek;
    Function* function;
    const char* path;
    const char* lineStart;
    ParseErrorCB errorCallback;
    void* userData;
    bool panic, hadError;
} Parser;

static void initParser(Parser* p, const char* path, const char* src, size_t len,
                       ParseErrorCB errorCallback, void* data) {
    p->panic = false;
    p->hadError = false;
    p->function = NULL;
    p->path = path;
    p->errorCallback = errorCallback;
    p->userData = data;
    jsrInitLexer(&p->lex, src, len);
    jsrNextToken(&p->lex, &p->peek);
}

static void beginFunction(Parser* p, Function* fn) {
    fn->isGenerator = false;
    fn->isCtor = false;
    fn->parent = p->function;
    p->function = fn;
}

static void markCurrentAsGenerator(Parser* p) {
    if(p->function) p->function->isGenerator = true;
}

static void endFunction(Parser* p) {
    JSR_ASSERT(p->function, "Mismatched `beginFunction` and `endFunction`");
    p->function = p->function->parent;
}

// -----------------------------------------------------------------------------
// ERROR REPORTING FUNCTIONS
// -----------------------------------------------------------------------------

static char* strchrWithNul(const char* str, char c) {
    char* ret;
    return (ret = strchr(str, c)) == NULL ? strchr(str, '\0') : ret;
}

static int vstrncatf(char* buf, size_t pos, size_t maxLen, const char* fmt, va_list ap) {
    size_t bufLen = pos >= maxLen ? 0 : maxLen - pos;
    return vsnprintf(buf + pos, bufLen, fmt, ap);
}

static int strncatf(char* buf, size_t pos, size_t maxLen, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int written = vstrncatf(buf, pos, maxLen, fmt, ap);
    va_end(ap);
    return written;
}

static int printSourceSnippet(char* buf, const char* line, int length, int coloumn) {
    int pos = 0;
    pos += strncatf(buf, pos, MAX_ERR_SIZE, "    %.*s\n", length, line);
    pos += strncatf(buf, pos, MAX_ERR_SIZE, "    ");
    for(int i = 0; i < coloumn; i++) {
        pos += strncatf(buf, pos, MAX_ERR_SIZE, " ");
    }
    pos += strncatf(buf, pos, MAX_ERR_SIZE, "^\n");
    return pos;
}

static void errorvfmt(Parser* p, JStarTok tok, const char* msg, va_list vargs) {
    if(p->panic) return;
    p->panic = p->hadError = true;

    if(p->errorCallback) {
        char error[MAX_ERR_SIZE];

        const char* lineStart = tok.lexeme - (tok.loc.col - 1);
        int lineLen = strchrWithNul(tok.lexeme, '\n') - lineStart;
        JSR_ASSERT(lineLen >= 0, "negative line length");
        int pos = printSourceSnippet(error, lineStart, lineLen, tok.loc.col - 1);

        va_list ap;
        va_copy(ap, vargs);
        pos += vstrncatf(error, pos, MAX_ERR_SIZE, msg, ap);
        va_end(ap);

        // Error message was too long, retry without source snippet
        if(pos >= MAX_ERR_SIZE) {
            pos = 0;
            pos += strncatf(error, pos, MAX_ERR_SIZE, "near `%s`: ", JStarTokName[tok.type]);

            va_list ap;
            va_copy(ap, vargs);
            pos += vstrncatf(error, pos, MAX_ERR_SIZE, msg, ap);
            va_end(ap);

            JSR_ASSERT(pos < MAX_ERR_SIZE, "Error message was truncated");
        }

        p->errorCallback(p->path, tok.loc, error, p->userData);
    }
}

static void errorTok(Parser* p, JStarTok tok, const char* msg, ...) {
    va_list ap;
    va_start(ap, msg);
    errorvfmt(p, tok, msg, ap);
    va_end(ap);
}

static void error(Parser* p, const char* msg, ...) {
    va_list ap;
    va_start(ap, msg);
    errorvfmt(p, p->peek, msg, ap);
    va_end(ap);
}

// -----------------------------------------------------------------------------
// UTILITY FUNCTIONS
// -----------------------------------------------------------------------------

static JStarIdentifier createIdentifier(JStarTok tok) {
    return (JStarIdentifier){tok.length, tok.lexeme};
}

static bool match(const Parser* p, JStarTokType type) {
    return p->peek.type == type;
}

static bool matchAny(const Parser* p, const JStarTokType* tokens, int count) {
    for(int i = 0; i < count; i++) {
        if(match(p, tokens[i])) return true;
    }
    return false;
}

static JStarTok advance(Parser* p) {
    JStarTok prev = p->peek;
    jsrNextToken(&p->lex, &p->peek);

    while(match(p, TOK_ERR) || match(p, TOK_UNTERMINATED_STR)) {
        error(p, p->peek.type == TOK_ERR ? "Invalid token" : "Unterminated String");
        prev = p->peek;
        jsrNextToken(&p->lex, &p->peek);
    }

    return prev;
}

static void skipNewLines(Parser* p) {
    while(match(p, TOK_NEWLINE)) {
        advance(p);
    }
}

static JStarTok require(Parser* p, JStarTokType type) {
    if(match(p, type)) {
        return advance(p);
    }
    error(p, "Expected token `%s`, instead `%s` found", JStarTokName[type],
          JStarTokName[p->peek.type]);
    return (JStarTok){0};
}

static void synchronize(Parser* p) {
    p->panic = false;
    while(!match(p, TOK_EOF)) {
        switch(p->peek.type) {
        case TOK_FUN:
        case TOK_AT:
        case TOK_VAR:
        case TOK_FOR:
        case TOK_IF:
        case TOK_WHILE:
        case TOK_RETURN:
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
        case TOK_AT:
            return;
        default:
            break;
        }
        advance(p);
    }
}

static JStarTokType assignToOperator(JStarTokType t) {
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
        JSR_UNREACHABLE();
    }
}

static bool isDeclaration(JStarTok tok) {
    JStarTokType t = tok.type;
    return t == TOK_FUN || t == TOK_NAT || t == TOK_CLASS || t == TOK_VAR;
}

static bool isCallExpression(const JStarExpr* e) {
    return (e->type == JSR_CALL) || (e->type == JSR_SUPER && e->as.sup.args);
}

static bool isImplicitEnd(JStarTok tok) {
    JStarTokType t = tok.type;
    return t == TOK_EOF || t == TOK_END || t == TOK_ELSE || t == TOK_ELIF || t == TOK_ENSURE ||
           t == TOK_EXCEPT;
}

static bool isStatementEnd(JStarTok tok) {
    return isImplicitEnd(tok) || tok.type == TOK_NEWLINE || tok.type == TOK_SEMICOLON;
}

static bool isExpressionStart(JStarTok tok) {
    JStarTokType t = tok.type;
    return t == TOK_NUMBER || t == TOK_TRUE || t == TOK_FALSE || t == TOK_IDENTIFIER ||
           t == TOK_STRING || t == TOK_NULL || t == TOK_SUPER || t == TOK_LPAREN ||
           t == TOK_LSQUARE || t == TOK_BANG || t == TOK_MINUS || t == TOK_FUN || t == TOK_PIPE ||
           t == TOK_HASH || t == TOK_HASH_HASH || t == TOK_LCURLY || t == TOK_YIELD;
}

static bool isAssign(JStarTok tok) {
    return tok.type >= TOK_EQUAL && tok.type <= TOK_MOD_EQ;
}

static bool isCompoundAssign(JStarTok tok) {
    return tok.type != TOK_EQUAL && isAssign(tok);
}

static bool isConstantLiteral(JStarExprType type) {
    return type == JSR_NUMBER || type == JSR_BOOL || type == JSR_STRING || type == JSR_NULL;
}

static bool isLValue(JStarExprType type) {
    return type == JSR_VAR || type == JSR_PROPERTY_ACCESS || type == JSR_INDEX;
}

static void checkUnpackAssignement(Parser* p, JStarExpr* lvals, JStarTokType assignToken) {
    if(assignToken != TOK_EQUAL) {
        error(p, "Unpack cannot use compound assignement");
        return;
    }
    arrayForeach(JStarExpr*, it, &lvals->as.exprList) {
        JStarExpr* expr = *it;
        if(expr && !isLValue(expr->type)) {
            error(p, "left hand side of unpack assignment must be composed of lvalues");
        }
    }
}

static void checkLvalue(Parser* p, JStarExpr* l, JStarTokType assignType) {
    if(l->type == JSR_TUPLE) {
        checkUnpackAssignement(p, l->as.tupleLiteral.exprs, assignType);
    } else if(!isLValue(l->type)) {
        error(p, "Left hand side of assignment must be an lvalue");
    }
}

static void requireStmtEnd(Parser* p) {
    if(!isImplicitEnd(p->peek)) {
        if(match(p, TOK_NEWLINE) || match(p, TOK_SEMICOLON)) {
            advance(p);
        } else {
            error(p, "Expected token `newline` or `;`");
        }
    }
}

static bool consumeSpreadOp(Parser* p) {
    if(match(p, TOK_ELLIPSIS)) {
        advance(p);
        return true;
    }
    return false;
}

// -----------------------------------------------------------------------------
// STATEMENTS PARSE
// -----------------------------------------------------------------------------

static JStarExpr* expression(Parser* p, bool parseTuple);
static JStarExpr* literal(Parser* p);
static JStarExpr* tupleLiteral(Parser* p);

static JStarFormalArg parseUnpackArgument(Parser* p) {
    JStarIdentifiers names = {0};
    JStarTok lparen = require(p, TOK_LPAREN);

    do {
        JStarTok id = require(p, TOK_IDENTIFIER);
        skipNewLines(p);

        arrayAppend(&names, createIdentifier(id));

        if(!match(p, TOK_COMMA)) {
            break;
        }

        advance(p);
        skipNewLines(p);
    } while(match(p, TOK_IDENTIFIER));

    require(p, TOK_RPAREN);

    return (JStarFormalArg){
        UNPACK,
        lparen.loc,
        {.unpack = names},
    };
}

static JStarFormalArgsList formalArgs(Parser* p, JStarTokType open, JStarTokType close) {
    JStarFormalArgsList args = {0};

    require(p, open);
    skipNewLines(p);

    while(match(p, TOK_IDENTIFIER) || match(p, TOK_LPAREN)) {
        JStarTok peek = p->peek;
        JStarFormalArg arg;

        if(peek.type == TOK_LPAREN) {
            arg = parseUnpackArgument(p);
        } else {
            JStarTok tok = advance(p);
            arg = (JStarFormalArg){
                SIMPLE,
                tok.loc,
                {.simple = createIdentifier(tok)},
            };
        }

        skipNewLines(p);

        if(match(p, TOK_EQUAL)) {
            if(arg.type == UNPACK) {
                error(p, "Unpack argument cannot have default value");
            }

            jsrLexRewind(&p->lex, &peek);
            jsrNextToken(&p->lex, &p->peek);
            break;
        }

        arrayAppend(&args.args, arg);

        if(!match(p, close)) {
            require(p, TOK_COMMA);
            skipNewLines(p);
        }
    }

    while(match(p, TOK_IDENTIFIER)) {
        JStarTok tok = advance(p);

        skipNewLines(p);
        require(p, TOK_EQUAL);
        skipNewLines(p);

        JStarExpr* constant = literal(p);
        skipNewLines(p);

        if(constant && !isConstantLiteral(constant->type)) {
            error(p, "Default argument must be a constant");
        }

        JStarFormalArg arg = {
            .type = SIMPLE,
            .loc = tok.loc,
            .as = {.simple = createIdentifier(tok)},
        };
        arrayAppend(&args.args, arg);
        arrayAppend(&args.defaults, constant);

        if(!match(p, close)) {
            require(p, TOK_COMMA);
            skipNewLines(p);
        }
    }

    if(match(p, TOK_ELLIPSIS)) {
        advance(p);
        skipNewLines(p);

        JStarTok vararg = require(p, TOK_IDENTIFIER);
        args.vararg = createIdentifier(vararg);
        skipNewLines(p);
    }

    require(p, close);
    return args;
}

static JStarStmt* parseStmt(Parser* p);

static JStarStmt* blockStmt(Parser* p) {
    JStarLoc loc = p->peek.loc;
    skipNewLines(p);

    JStarStmts stmts = {0};
    while(!isImplicitEnd(p->peek)) {
        arrayAppend(&stmts, parseStmt(p));
        skipNewLines(p);
    }

    return jsrBlockStmt(loc, stmts);
}

static JStarStmt* ifBody(Parser* p, JStarLoc loc) {
    JStarExpr* cond = expression(p, true);
    JStarStmt* thenBody = blockStmt(p);
    JStarStmt* elseBody = NULL;

    if(match(p, TOK_ELIF)) {
        JStarLoc loc = p->peek.loc;
        advance(p);
        skipNewLines(p);
        elseBody = ifBody(p, loc);
    } else if(match(p, TOK_ELSE)) {
        advance(p);
        elseBody = blockStmt(p);
    }

    return jsrIfStmt(loc, cond, thenBody, elseBody);
}

static JStarStmt* ifStmt(Parser* p) {
    JStarLoc loc = p->peek.loc;

    advance(p);
    skipNewLines(p);

    JStarStmt* ifStmt = ifBody(p, loc);
    require(p, TOK_END);

    return ifStmt;
}

static JStarStmt* whileStmt(Parser* p) {
    JStarLoc loc = p->peek.loc;

    advance(p);
    skipNewLines(p);

    JStarExpr* cond = expression(p, true);
    JStarStmt* body = blockStmt(p);
    require(p, TOK_END);

    return jsrWhileStmt(loc, cond, body);
}

static JStarStmt* varDecl(Parser* p) {
    JStarLoc loc = p->peek.loc;

    advance(p);
    skipNewLines(p);

    bool isUnpack = false;
    JStarIdentifiers identifiers = {0};

    do {
        JStarTok id = require(p, TOK_IDENTIFIER);
        arrayAppend(&identifiers, createIdentifier(id));

        if(match(p, TOK_COMMA)) {
            advance(p);
            isUnpack = true;
        } else {
            break;
        }
    } while(match(p, TOK_IDENTIFIER));

    JStarExpr* init = NULL;
    if(match(p, TOK_EQUAL)) {
        advance(p);
        skipNewLines(p);
        init = expression(p, true);
    }

    return jsrVarDecl(loc, isUnpack, identifiers, init);
}

static JStarStmt* forEach(Parser* p, JStarTok tok, JStarStmt* var, JStarLoc loc) {
    if(var->as.decl.as.var.init != NULL) {
        errorTok(p, tok, "Variable declaration in foreach cannot have initializer");
    }

    advance(p);
    skipNewLines(p);

    JStarExpr* e = expression(p, true);
    JStarStmt* body = blockStmt(p);
    require(p, TOK_END);

    return jsrForEachStmt(loc, var, e, body);
}

static JStarStmt* forStmt(Parser* p) {
    JStarLoc loc = p->peek.loc;

    advance(p);
    skipNewLines(p);

    JStarStmt* init = NULL;
    if(!match(p, TOK_SEMICOLON)) {
        if(match(p, TOK_VAR)) {
            JStarTok varTok = p->peek;
            init = varDecl(p);
            skipNewLines(p);
            if(match(p, TOK_IN)) {
                return forEach(p, varTok, init, loc);
            }
        } else {
            JStarExpr* e = expression(p, true);
            init = jsrExprStmt(e->loc, e);
        }
    }

    require(p, TOK_SEMICOLON);

    JStarExpr* cond = NULL;
    if(!match(p, TOK_SEMICOLON)) cond = expression(p, true);
    require(p, TOK_SEMICOLON);

    JStarExpr* act = NULL;
    if(isExpressionStart(p->peek)) act = expression(p, true);

    JStarStmt* body = blockStmt(p);
    require(p, TOK_END);

    return jsrForStmt(loc, init, cond, act, body);
}

static JStarStmt* returnStmt(Parser* p) {
    if(!p->function) {
        error(p, "Cannot use return outside a function");
    }

    JStarLoc loc = p->peek.loc;
    advance(p);

    JStarExpr* e = NULL;
    if(!isStatementEnd(p->peek)) {
        e = expression(p, true);
    }

    requireStmtEnd(p);
    return jsrReturnStmt(loc, e);
}

static JStarStmt* importStmt(Parser* p) {
    JStarLoc loc = p->peek.loc;

    advance(p);
    skipNewLines(p);

    JStarIdentifiers modules = {0};

    for(;;) {
        JStarTok name = require(p, TOK_IDENTIFIER);
        arrayAppend(&modules, createIdentifier(name));
        if(!match(p, TOK_DOT)) break;
        advance(p);
        skipNewLines(p);
    }

    JStarIdentifier asName = {0};
    JStarIdentifiers importNames = {0};

    if(match(p, TOK_FOR)) {
        advance(p);
        skipNewLines(p);

        for(;;) {
            JStarTok name = require(p, TOK_IDENTIFIER);
            arrayAppend(&importNames, createIdentifier(name));
            if(!match(p, TOK_COMMA)) break;
            advance(p);
            skipNewLines(p);
        }
    } else if(match(p, TOK_AS)) {
        advance(p);
        skipNewLines(p);
        JStarTok as = require(p, TOK_IDENTIFIER);
        asName = createIdentifier(as);
    }

    requireStmtEnd(p);
    return jsrImportStmt(loc, modules, importNames, asName);
}

static JStarStmt* tryStmt(Parser* p) {
    JStarLoc loc = p->peek.loc;
    advance(p);

    JStarStmt* tryBlock = blockStmt(p);
    JStarStmts excs = {0};
    JStarStmt* ensure = NULL;

    while(match(p, TOK_EXCEPT)) {
        JStarLoc excLoc = p->peek.loc;
        advance(p);
        skipNewLines(p);

        JStarExpr* cls = expression(p, true);
        skipNewLines(p);

        JStarTok var = require(p, TOK_IDENTIFIER);
        JStarIdentifier varName = createIdentifier(var);

        JStarStmt* block = blockStmt(p);
        arrayAppend(&excs, jsrExceptStmt(excLoc, cls, varName, block));
    }

    if(match(p, TOK_ENSURE)) {
        advance(p);
        ensure = blockStmt(p);
    }

    if(!excs.count && !ensure) {
        error(p, "Expected except or ensure clause");
    }

    require(p, TOK_END);
    return jsrTryStmt(loc, tryBlock, excs, ensure);
}

static JStarStmt* raiseStmt(Parser* p) {
    JStarLoc loc = p->peek.loc;

    advance(p);
    skipNewLines(p);

    JStarExpr* exc = expression(p, true);
    requireStmtEnd(p);

    return jsrRaiseStmt(loc, exc);
}

static JStarStmt* withStmt(Parser* p) {
    JStarLoc loc = p->peek.loc;

    advance(p);
    skipNewLines(p);

    JStarExpr* e = expression(p, true);
    skipNewLines(p);

    JStarTok var = require(p, TOK_IDENTIFIER);
    JStarIdentifier varName = createIdentifier(var);
    JStarStmt* block = blockStmt(p);
    require(p, TOK_END);

    return jsrWithStmt(loc, e, varName, block);
}

static JStarExprs parseDecorators(Parser* p) {
    JStarExprs decorators = {0};
    while(match(p, TOK_AT)) {
        advance(p);
        JStarExpr* expr = expression(p, false);
        arrayAppend(&decorators, expr);
        skipNewLines(p);
    }
    return decorators;
}

static void freeDecorators(JStarExprs* decorators) {
    arrayForeach(JStarExpr*, e, decorators) {
        jsrExprFree(*e);
    }
    arrayFree(decorators);
}

static JStarIdentifier ctorName(JStarLoc loc) {
    return (JStarIdentifier){strlen(CONSTRUCT_ID), CONSTRUCT_ID};
}

static JStarStmt* funcDecl(Parser* p, bool parseCtor) {
    Function fn;
    beginFunction(p, &fn);

    JStarTok funTok = advance(p);
    skipNewLines(p);

    JStarIdentifier funcName;
    if(parseCtor && funTok.type == TOK_CTOR) {
        fn.isCtor = true;
        funcName = ctorName(funTok.loc);
    } else {
        JStarTok fun = require(p, TOK_IDENTIFIER);
        funcName = createIdentifier(fun);
    }

    skipNewLines(p);

    JStarFormalArgsList args = formalArgs(p, TOK_LPAREN, TOK_RPAREN);
    JStarStmt* body = blockStmt(p);
    require(p, TOK_END);

    JStarStmt* decl = jsrFuncDecl(funTok.loc, funcName, args, p->function->isGenerator, body);

    endFunction(p);

    return decl;
}

static JStarStmt* nativeDecl(Parser* p, bool parseCtor) {
    JStarLoc loc = p->peek.loc;

    advance(p);
    skipNewLines(p);

    JStarIdentifier nativeName;
    if(parseCtor && p->peek.type == TOK_CTOR) {
        JStarTok ctor = advance(p);
        nativeName = ctorName(ctor.loc);
    } else {
        JStarTok nat = require(p, TOK_IDENTIFIER);
        nativeName = createIdentifier(nat);
    }

    skipNewLines(p);

    JStarFormalArgsList args = formalArgs(p, TOK_LPAREN, TOK_RPAREN);
    requireStmtEnd(p);

    return jsrNativeDecl(loc, nativeName, args);
}

static JStarStmt* classDecl(Parser* p) {
    JStarLoc loc = p->peek.loc;

    advance(p);
    skipNewLines(p);

    JStarTok cls = require(p, TOK_IDENTIFIER);
    JStarIdentifier clsName = createIdentifier(cls);
    skipNewLines(p);

    JStarExpr* sup = NULL;
    if(match(p, TOK_IS)) {
        advance(p);
        skipNewLines(p);

        sup = expression(p, true);
        skipNewLines(p);

        if(p->panic) classSynchronize(p);
    }

    skipNewLines(p);

    JStarStmts methods = {0};
    while(!match(p, TOK_END) && !match(p, TOK_EOF)) {
        JStarExprs decorators = parseDecorators(p);
        JStarStmt* method = NULL;

        switch(p->peek.type) {
        case TOK_NAT:
            method = nativeDecl(p, true);
            method->as.decl.decorators = decorators;
            break;
        case TOK_CTOR:
        case TOK_FUN:
            method = funcDecl(p, true);
            method->as.decl.decorators = decorators;
            break;
        default:
            error(p, "Expected function or native delcaration");
            advance(p);
            break;
        }

        arrayAppend(&methods, method);
        skipNewLines(p);
        if(p->panic) classSynchronize(p);
    }

    require(p, TOK_END);
    return jsrClassDecl(loc, clsName, sup, methods);
}

static JStarStmt* staticDecl(Parser* p) {
    advance(p);
    skipNewLines(p);

    if(!isDeclaration(p->peek)) {
        error(p, "Only a declaration can be annotated as `static`");
        return NULL;
    }

    JStarStmt* decl = parseStmt(p);
    decl->as.decl.isStatic = true;

    return decl;
}

static JStarStmt* decoratedDecl(Parser* p) {
    JStarExprs decorators = parseDecorators(p);

    if(!match(p, TOK_STATIC) && !isDeclaration(p->peek)) {
        error(p, "Decorators can only be applied to declarations");
        freeDecorators(&decorators);
        return NULL;
    }

    JStarStmt* decl = parseStmt(p);
    decl->as.decl.decorators = decorators;

    return decl;
}

static JStarExpr* assignmentExpr(Parser* p, JStarExpr* l, bool parseTuple);

static JStarStmt* exprStmt(Parser* p) {
    JStarTok tok = p->peek;
    JStarExpr* l = tupleLiteral(p);

    if(!isAssign(p->peek) && !isCallExpression(l) && l->type != JSR_YIELD) {
        errorTok(p, tok, "Expression result unused, consider adding a variable declaration");
    }

    if(isAssign(p->peek)) {
        l = assignmentExpr(p, l, true);
    }

    return jsrExprStmt(l->loc, l);
}

static JStarStmt* parseStmt(Parser* p) {
    JStarLoc loc = p->peek.loc;

    switch(p->peek.type) {
    case TOK_CLASS:
        return classDecl(p);
    case TOK_NAT:
        return nativeDecl(p, false);
    case TOK_FUN:
        return funcDecl(p, false);
    case TOK_VAR: {
        JStarStmt* var = varDecl(p);
        requireStmtEnd(p);
        return var;
    }
    case TOK_STATIC:
        return staticDecl(p);
    case TOK_AT:
        return decoratedDecl(p);
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
    case TOK_BEGIN: {
        require(p, TOK_BEGIN);
        JStarStmt* block = blockStmt(p);
        require(p, TOK_END);
        return block;
    }
    case TOK_CONTINUE:
        advance(p);
        requireStmtEnd(p);
        return jsrContinueStmt(loc);
    case TOK_BREAK:
        advance(p);
        requireStmtEnd(p);
        return jsrBreakStmt(loc);
    default:
        break;
    }

    JStarStmt* stmt = exprStmt(p);
    requireStmtEnd(p);
    return stmt;
}

static JStarStmt* parseProgram(Parser* p) {
    skipNewLines(p);

    JStarStmts stmts = {0};
    while(!match(p, TOK_EOF)) {
        arrayAppend(&stmts, parseStmt(p));
        skipNewLines(p);
        if(p->panic) synchronize(p);
    }

    // Top level function doesn't have name or arguments, so pass them empty
    JStarFormalArgsList args = {0};
    JStarLoc loc = {0};
    return jsrFuncDecl(loc, (JStarIdentifier){0}, args, false, jsrBlockStmt(loc, stmts));
}

// -----------------------------------------------------------------------------
// EXPRESSIONS PARSE
// -----------------------------------------------------------------------------

static JStarExpr* expressionLst(Parser* p, JStarTokType open, JStarTokType close) {
    JStarLoc loc = p->peek.loc;

    require(p, open);
    skipNewLines(p);

    JStarExprs exprs = {0};
    while(!match(p, close)) {
        bool isSpread = consumeSpreadOp(p);
        JStarExpr* e = expression(p, false);
        skipNewLines(p);

        if(isSpread) {
            e = jsrSpreadExpr(loc, e);
        }

        arrayAppend(&exprs, e);

        if(!match(p, TOK_COMMA)) {
            break;
        }

        advance(p);
        skipNewLines(p);
    }

    require(p, close);
    return jsrExprList(loc, exprs);
}

static JStarExpr* parseTableLiteral(Parser* p) {
    JStarLoc loc = p->peek.loc;

    advance(p);
    skipNewLines(p);

    JStarExprs keyVals = {0};
    while(!match(p, TOK_RCURLY)) {
        bool isSpread = consumeSpreadOp(p);

        if(isSpread) {
            JStarExpr* expr = expression(p, false);
            skipNewLines(p);
            expr = jsrSpreadExpr(expr->loc, expr);
            arrayAppend(&keyVals, expr);
        } else {
            JStarExpr* key;
            if(match(p, TOK_DOT)) {
                advance(p);
                JStarTok id = require(p, TOK_IDENTIFIER);
                key = jsrStrLiteral(id.loc, id.lexeme, id.length);
            } else {
                key = expression(p, false);
            }

            skipNewLines(p);
            require(p, TOK_COLON);
            skipNewLines(p);

            JStarExpr* val = expression(p, false);
            skipNewLines(p);

            if(p->hadError) {
                break;
            }

            arrayAppend(&keyVals, key);
            arrayAppend(&keyVals, val);
        }

        if(!match(p, TOK_RCURLY)) {
            require(p, TOK_COMMA);
            skipNewLines(p);
        }
    }

    require(p, TOK_RCURLY);
    return jsrTableLiteral(loc, jsrExprList(loc, keyVals));
}

static JStarExpr* parseSuperLiteral(Parser* p) {
    JStarLoc loc = p->peek.loc;
    advance(p);

    JStarTok name = {0};
    JStarExpr* args = NULL;

    if(match(p, TOK_DOT)) {
        advance(p);
        skipNewLines(p);
        name = require(p, TOK_IDENTIFIER);
    }

    if(match(p, TOK_LPAREN)) {
        args = expressionLst(p, TOK_LPAREN, TOK_RPAREN);
    } else if(match(p, TOK_LCURLY)) {
        JStarExprs tableCallArgs = {0};
        arrayAppend(&tableCallArgs, parseTableLiteral(p));
        args = jsrExprList(loc, tableCallArgs);
    }

    return jsrSuperLiteral(loc, &name, args);
}

JStarExpr* parseListLiteral(Parser* p) {
    JStarLoc loc = p->peek.loc;
    JStarExpr* exprs = expressionLst(p, TOK_LSQUARE, TOK_RSQUARE);
    return jsrListLiteral(loc, exprs);
}

static JStarExpr* literal(Parser* p) {
    JStarLoc loc = p->peek.loc;
    JStarTok* tok = &p->peek;

    switch(tok->type) {
    case TOK_SUPER:
        return parseSuperLiteral(p);
    case TOK_LCURLY:
        return parseTableLiteral(p);
    case TOK_LSQUARE:
        return parseListLiteral(p);
    case TOK_TRUE:
        advance(p);
        return jsrBoolLiteral(loc, true);
    case TOK_FALSE:
        advance(p);
        return jsrBoolLiteral(loc, false);
    case TOK_NULL:
        advance(p);
        return jsrNullLiteral(loc);
    case TOK_NUMBER: {
        JStarExpr* e = jsrNumLiteral(loc, strtod(tok->lexeme, NULL));
        advance(p);
        return e;
    }
    case TOK_IDENTIFIER: {
        JStarExpr* e = jsrVarLiteral(loc, tok->lexeme, tok->length);
        advance(p);
        return e;
    }
    case TOK_STRING: {
        JStarExpr* e = jsrStrLiteral(loc, tok->lexeme + 1, tok->length - 2);
        advance(p);
        return e;
    }
    case TOK_LPAREN: {
        advance(p);
        skipNewLines(p);

        if(match(p, TOK_RPAREN)) {
            advance(p);
            return jsrTupleLiteral(loc, jsrExprList(loc, (JStarExprs){0}));
        }

        JStarExpr* e = expression(p, true);

        skipNewLines(p);
        require(p, TOK_RPAREN);

        return e;
    }
    case TOK_UNTERMINATED_STR:
        error(p, "Unterminated String");
        advance(p);
        break;
    case TOK_ERR:
        error(p, "Invalid token");
        advance(p);
        break;
    default:
        error(p, "Expected expression");
        advance(p);
        break;
    }

    // Return dummy expression to avoid NULL
    return jsrNullLiteral(loc);
}

static JStarExpr* postfixExpr(Parser* p) {
    JStarExpr* lit = literal(p);

    JStarTokType tokens[] = {TOK_LPAREN, TOK_LCURLY, TOK_DOT, TOK_LSQUARE};
    while(matchAny(p, tokens, ARRAY_SIZE(tokens))) {
        JStarLoc loc = p->peek.loc;
        switch(p->peek.type) {
        case TOK_DOT: {
            advance(p);
            skipNewLines(p);
            JStarTok attr = require(p, TOK_IDENTIFIER);
            lit = jsrPropertyAccessExpr(loc, lit, attr.lexeme, attr.length);
            break;
        }
        case TOK_LCURLY: {
            JStarExprs tableCallArgs = {0};
            arrayAppend(&tableCallArgs, parseTableLiteral(p));
            JStarExpr* args = jsrExprList(loc, tableCallArgs);
            lit = jsrCallExpr(loc, lit, args);
            break;
        }
        case TOK_LSQUARE: {
            require(p, TOK_LSQUARE);
            skipNewLines(p);
            lit = jsrIndexExpr(loc, lit, expression(p, true));
            skipNewLines(p);
            require(p, TOK_RSQUARE);
            break;
        }
        case TOK_LPAREN: {
            JStarExpr* args = expressionLst(p, TOK_LPAREN, TOK_RPAREN);
            lit = jsrCallExpr(loc, lit, args);
            break;
        }
        default:
            break;
        }
    }

    return lit;
}

static JStarExpr* unaryExpr(Parser* p);

static JStarExpr* powExpr(Parser* p) {
    JStarExpr* base = postfixExpr(p);

    while(match(p, TOK_POW)) {
        JStarTok powOp = advance(p);
        skipNewLines(p);
        JStarExpr* exp = unaryExpr(p);
        base = jsrPowExpr(powOp.loc, base, exp);
    }

    return base;
}

static JStarExpr* unaryExpr(Parser* p) {
    JStarTokType tokens[] = {TOK_TILDE, TOK_BANG, TOK_MINUS, TOK_HASH, TOK_HASH_HASH};

    if(matchAny(p, tokens, ARRAY_SIZE(tokens))) {
        JStarTok op = advance(p);
        skipNewLines(p);
        return jsrUnaryExpr(op.loc, op.type, unaryExpr(p));
    }

    return powExpr(p);
}

static JStarExpr* parseBinary(Parser* p, const JStarTokType* tokens, int count,
                              JStarExpr* (*operand)(Parser*)) {
    JStarExpr* l = (*operand)(p);

    while(matchAny(p, tokens, count)) {
        JStarTok op = advance(p);
        skipNewLines(p);
        JStarExpr* r = (*operand)(p);
        l = jsrBinaryExpr(op.loc, op.type, l, r);
    }

    return l;
}

static JStarExpr* multiplicativeExpr(Parser* p) {
    JStarTokType tokens[] = {TOK_MULT, TOK_DIV, TOK_MOD};
    return parseBinary(p, tokens, ARRAY_SIZE(tokens), unaryExpr);
}

static JStarExpr* additiveExpr(Parser* p) {
    JStarTokType tokens[] = {TOK_PLUS, TOK_MINUS};
    return parseBinary(p, tokens, ARRAY_SIZE(tokens), multiplicativeExpr);
}

static JStarExpr* shiftExpr(Parser* p) {
    JStarTokType tokens[] = {TOK_LSHIFT, TOK_RSHIFT};
    return parseBinary(p, tokens, ARRAY_SIZE(tokens), additiveExpr);
}

static JStarExpr* bandExpr(Parser* p) {
    JStarTokType tokens[] = {TOK_AMPER};
    return parseBinary(p, tokens, ARRAY_SIZE(tokens), shiftExpr);
}

static JStarExpr* xorExpr(Parser* p) {
    JStarTokType tokens[] = {TOK_TILDE};
    return parseBinary(p, tokens, ARRAY_SIZE(tokens), bandExpr);
}

static JStarExpr* borExpr(Parser* p) {
    JStarTokType tokens[] = {TOK_PIPE};
    return parseBinary(p, tokens, ARRAY_SIZE(tokens), xorExpr);
}

static JStarExpr* relationalExpr(Parser* p) {
    JStarTokType tokens[] = {TOK_EQUAL_EQUAL, TOK_BANG_EQ, TOK_GT, TOK_GE, TOK_LT, TOK_LE, TOK_IS};
    return parseBinary(p, tokens, ARRAY_SIZE(tokens), borExpr);
}

static JStarExpr* andExpr(Parser* p) {
    JStarTokType tokens[] = {TOK_AND};
    return parseBinary(p, tokens, ARRAY_SIZE(tokens), relationalExpr);
}

static JStarExpr* orExpr(Parser* p) {
    JStarTokType tokens[] = {TOK_OR};
    return parseBinary(p, tokens, ARRAY_SIZE(tokens), andExpr);
}

static JStarExpr* ternaryExpr(Parser* p) {
    JStarLoc loc = p->peek.loc;
    JStarExpr* expr = orExpr(p);

    if(match(p, TOK_IF)) {
        advance(p);
        skipNewLines(p);

        JStarExpr* cond = ternaryExpr(p);

        require(p, TOK_ELSE);
        skipNewLines(p);

        JStarExpr* elseExpr = ternaryExpr(p);
        return jsrTernaryExpr(loc, cond, expr, elseExpr);
    }

    return expr;
}

static JStarExpr* funcLiteral(Parser* p) {
    if(match(p, TOK_FUN)) {
        Function fn;
        beginFunction(p, &fn);

        JStarLoc loc = p->peek.loc;

        require(p, TOK_FUN);
        skipNewLines(p);

        JStarFormalArgsList args = formalArgs(p, TOK_LPAREN, TOK_RPAREN);
        JStarStmt* body = blockStmt(p);
        require(p, TOK_END);

        JStarExpr* lit = jsrFunLiteral(loc, args, p->function->isGenerator, body);

        endFunction(p);

        return lit;
    }
    if(match(p, TOK_PIPE)) {
        Function fn;
        beginFunction(p, &fn);

        JStarLoc loc = p->peek.loc;
        JStarFormalArgsList args = formalArgs(p, TOK_PIPE, TOK_PIPE);
        skipNewLines(p);

        require(p, TOK_ARROW);
        skipNewLines(p);

        JStarExpr* e = expression(p, false);
        JStarStmts anonFuncStmts = {0};
        arrayAppend(&anonFuncStmts, jsrReturnStmt(loc, e));
        JStarStmt* body = jsrBlockStmt(loc, anonFuncStmts);

        JStarExpr* lit = jsrFunLiteral(loc, args, p->function->isGenerator, body);

        endFunction(p);

        return lit;
    }
    return ternaryExpr(p);
}

static JStarExpr* yieldExpr(Parser* p) {
    if(match(p, TOK_YIELD)) {
        if(!p->function) {
            error(p, "Cannot use yield outside of a function");
        } else if(p->function->isCtor) {
            error(p, "Cannot yield inside a constructor");
        }

        markCurrentAsGenerator(p);

        JStarTok yield = advance(p);
        JStarExpr* expr = NULL;

        if(isExpressionStart(p->peek)) {
            expr = expression(p, false);
        }

        return jsrYieldExpr(yield.loc, expr);
    }
    return funcLiteral(p);
}

static JStarExpr* tupleLiteral(Parser* p) {
    JStarLoc loc = p->peek.loc;

    bool isSpread = consumeSpreadOp(p);
    JStarExpr* e = yieldExpr(p);

    if(isSpread) {
        if(!match(p, TOK_COMMA)) {
            error(p,
                  "Cannot use spread operator here. Consider adding a `,` to make this a Tuple "
                  "literal");
        }
        e = jsrSpreadExpr(e->loc, e);
    }

    if(match(p, TOK_COMMA)) {
        JStarExprs exprs = {0};
        arrayAppend(&exprs, e);

        while(match(p, TOK_COMMA)) {
            advance(p);

            if(!isExpressionStart(p->peek) && p->peek.type != TOK_ELLIPSIS) {
                break;
            }

            bool isSpread = consumeSpreadOp(p);
            JStarExpr* e = yieldExpr(p);

            if(isSpread) {
                e = jsrSpreadExpr(e->loc, e);
            }

            arrayAppend(&exprs, e);
        }

        e = jsrTupleLiteral(loc, jsrExprList(loc, exprs));
    }

    return e;
}

static JStarExpr* assignmentExpr(Parser* p, JStarExpr* l, bool parseTuple) {
    checkLvalue(p, l, p->peek.type);
    JStarTok assignToken = advance(p);
    JStarExpr* r = expression(p, parseTuple);

    if(isCompoundAssign(assignToken)) {
        JStarTokType op = assignToOperator(assignToken.type);
        l = jsrCompundAssignExpr(assignToken.loc, op, l, r);
    } else {
        l = jsrAssignExpr(assignToken.loc, l, r);
    }

    return l;
}

static JStarExpr* expression(Parser* p, bool parseTuple) {
    JStarExpr* l = parseTuple ? tupleLiteral(p) : yieldExpr(p);

    if(isAssign(p->peek)) {
        return assignmentExpr(p, l, parseTuple);
    }

    return l;
}

// -----------------------------------------------------------------------------
// API
// -----------------------------------------------------------------------------

JStarStmt* jsrParse(const char* path, const char* src, size_t len, ParseErrorCB errFn, void* data) {
    PROFILE_FUNC()

    Parser p;
    initParser(&p, path, src, len, errFn, data);

    JStarStmt* program = parseProgram(&p);
    skipNewLines(&p);

    if(!match(&p, TOK_EOF)) {
        error(&p, "Unexpected token");
    }

    if(p.hadError) {
        jsrStmtFree(program);
        return NULL;
    }

    return program;
}

JStarExpr* jsrParseExpression(const char* path, const char* src, size_t len, ParseErrorCB errFn,
                              void* data) {
    PROFILE_FUNC()

    Parser p;
    initParser(&p, path, src, len, errFn, data);

    JStarExpr* expr = expression(&p, true);
    skipNewLines(&p);

    if(!match(&p, TOK_EOF)) {
        error(&p, "Unexpected token");
    }

    if(p.hadError) {
        jsrExprFree(expr);
        return NULL;
    }

    return expr;
}
