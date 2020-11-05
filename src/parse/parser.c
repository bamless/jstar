#include "parse/parser.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "parse/ast.h"
#include "parse/lex.h"
#include "parse/vector.h"
#include "util.h"

#define MAX_ERR_SIZE 512

typedef struct Parser {
    JStarLex lex;
    JStarTok peek;
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
    jsrInitLexer(&p->lex, src);
    jsrNextToken(&p->lex, &p->peek);
    p->lineStart = p->peek.lexeme;
}

// -----------------------------------------------------------------------------
// ERROR REPORTING FUNCTIONS
// -----------------------------------------------------------------------------

static char* strchrnul(const char* str, char c) {
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

static int printSourceSnippet(char* buf, const char* line, size_t lineLen, int coloumn) {
    int pos = 0;
    pos += strncatf(buf, pos, MAX_ERR_SIZE, "    %.*s\n", lineLen, line);
    pos += strncatf(buf, pos, MAX_ERR_SIZE, "    ");
    for(int i = 0; i < coloumn; i++) {
        pos += strncatf(buf, pos, MAX_ERR_SIZE, " ");
    }
    pos += strncatf(buf, pos, MAX_ERR_SIZE, "^\n");
    return pos;
}

const char* correctEscapedNewlines(const char* line, const char* errorTokLexeme) {
    const char* newline = line;
    while((newline = strchr(newline, '\n')) && newline < errorTokLexeme) {
        line = ++newline;
    }
    return line;
}

static void error(Parser* p, const char* msg, ...) {
    if(p->panic) return;
    p->panic = p->hadError = true;

    if(p->errorCallback) {
        JStarTok* errorTok = &p->peek;
        char error[MAX_ERR_SIZE];

        p->lineStart = correctEscapedNewlines(p->lineStart, errorTok->lexeme);
        int tokenCol = errorTok->lexeme - p->lineStart;
        int lineLen = strchrnul(errorTok->lexeme, '\n') - p->lineStart;

        // print error message with source snippet
        int pos = printSourceSnippet(error, p->lineStart, lineLen, tokenCol);
        va_list ap;
        va_start(ap, msg);
        pos += vstrncatf(error, pos, MAX_ERR_SIZE, msg, ap);
        va_end(ap);

        // Error message was too long, retry without source snippet
        if(pos >= MAX_ERR_SIZE) {
            pos = 0;
            const char* tokenStr = JStarTokName[errorTok->type];
            pos += strncatf(error, pos, MAX_ERR_SIZE, "[col:%d] Near `%s`: ", tokenCol, tokenStr);

            va_list ap;
            va_start(ap, msg);
            pos += vstrncatf(error, pos, MAX_ERR_SIZE, msg, ap);
            va_end(ap);

            // TODO: use growable buffer to print error?
            ASSERT(pos < MAX_ERR_SIZE, "Error message was truncated");
        }

        p->errorCallback(p->path, p->peek.line, error);
    }
}

// -----------------------------------------------------------------------------
// UTILITY FUNCTIONS
// -----------------------------------------------------------------------------

static bool match(Parser* p, JStarTokType type) {
    return p->peek.type == type;
}

static bool matchAny(Parser* p, JStarTokType* tokens, int count) {
    for(int i = 0; i < count; i++) {
        if(match(p, tokens[i])) return true;
    }
    return false;
}

static JStarTok advance(Parser* p) {
    JStarTok prev = p->peek;
    jsrNextToken(&p->lex, &p->peek);

    if(prev.type == TOK_NEWLINE) {
        p->lineStart = p->peek.lexeme;
    }

    while(match(p, TOK_ERR) || match(p, TOK_UNTERMINATED_STR)) {
        error(p, p->peek.type == TOK_ERR ? "Invalid token." : "Unterminated string.");
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

    error(p, "Expected token `%s`, instead `%s` found.", JStarTokName[type],
          JStarTokName[p->peek.type]);
    return (JStarTok){0, NULL, 0, 0};
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
        UNREACHABLE();
        return -1;
    }
}

static bool isCallExpression(JStarExpr* e) {
    return (e->type == JSR_CALL) || (e->type == JSR_SUPER && e->as.sup.args);
}

static bool isImplicitEnd(JStarTok* tok) {
    JStarTokType t = tok->type;
    return t == TOK_EOF || t == TOK_END || t == TOK_ELSE || t == TOK_ELIF || t == TOK_ENSURE ||
           t == TOK_EXCEPT;
}

static bool isStatementEnd(JStarTok* tok) {
    return isImplicitEnd(tok) || tok->type == TOK_NEWLINE || tok->type == TOK_SEMICOLON;
}

static bool isExpressionStart(JStarTok* tok) {
    JStarTokType t = tok->type;
    return t == TOK_NUMBER || t == TOK_TRUE || t == TOK_FALSE || t == TOK_IDENTIFIER ||
           t == TOK_STRING || t == TOK_NULL || t == TOK_SUPER || t == TOK_LPAREN ||
           t == TOK_LSQUARE || t == TOK_BANG || t == TOK_MINUS || t == TOK_FUN || t == TOK_HASH ||
           t == TOK_HASH_HASH || t == TOK_LCURLY;
}

static bool isAssign(JStarTok* tok) {
    return tok->type >= TOK_EQUAL && tok->type <= TOK_MOD_EQ;
}

static bool isCompoundAssign(JStarTok* tok) {
    return tok->type != TOK_EQUAL && isAssign(tok);
}

static bool isConstantLiteral(JStarExprType type) {
    return type == JSR_NUMBER || type == JSR_BOOL || type == JSR_STRING || type == JSR_NULL;
}

static bool isLValue(JStarExprType type) {
    return type == JSR_VAR || type == JSR_ACCESS || type == JSR_ARR_ACCESS;
}

static void checkUnpackAssignement(Parser* p, JStarExpr* lvals, JStarTokType assignToken) {
    if(assignToken != TOK_EQUAL) {
        error(p, "Unpack cannot use compound assignement.");
        return;
    }
    vecForeach(JStarExpr(**it), lvals->as.list) {
        JStarExpr* expr = *it;
        if(expr && !isLValue(expr->type)) {
            error(p, "Left hand side of unpack assignment must be composed of lvalues.");
        }
    }
}

static void checkLvalue(Parser* p, JStarExpr* l, JStarTokType assignType) {
    if(l->type == JSR_TUPLE) {
        checkUnpackAssignement(p, l->as.tuple.exprs, assignType);
    } else if(!isLValue(l->type)) {
        error(p, "Left hand side of assignment must be an lvalue.");
    }
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

static JStarExpr* expression(Parser* p, bool parseTuple);
static JStarExpr* literal(Parser* p);
static JStarExpr* tupleLiteral(Parser* p);

typedef struct {
    Vector arguments;
    Vector defaults;
    bool isVararg;
} FormalArgs;

static FormalArgs formalArgs(Parser* p, JStarTokType open, JStarTokType close) {
    FormalArgs args = {0};

    require(p, open);
    skipNewLines(p);

    while(match(p, TOK_IDENTIFIER)) {
        JStarTok argument = advance(p);
        skipNewLines(p);

        if(match(p, TOK_EQUAL)) {
            jsrLexRewind(&p->lex, &argument);
            jsrNextToken(&p->lex, &p->peek);
            break;
        }

        vecPush(&args.arguments, jsrNewIdentifier(argument.length, argument.lexeme));
        skipNewLines(p);

        if(!match(p, close)) {
            require(p, TOK_COMMA);
            skipNewLines(p);
        }
    }

    while(match(p, TOK_IDENTIFIER)) {
        JStarTok argument = advance(p);

        skipNewLines(p);
        require(p, TOK_EQUAL);
        skipNewLines(p);

        JStarExpr* constant = literal(p);
        skipNewLines(p);

        if(constant && !isConstantLiteral(constant->type)) {
            error(p, "Default argument must be a constant");
        }

        vecPush(&args.arguments, jsrNewIdentifier(argument.length, argument.lexeme));
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

static JStarStmt* parseStmt(Parser* p);

static JStarStmt* blockStmt(Parser* p) {
    int line = p->peek.line;
    skipNewLines(p);

    Vector stmts = vecNew();
    while(!isImplicitEnd(&p->peek)) {
        vecPush(&stmts, parseStmt(p));
        skipNewLines(p);
    }

    return jsrBlockStmt(line, &stmts);
}

static JStarStmt* ifBody(Parser* p, int line) {
    JStarExpr* cond = expression(p, true);
    skipNewLines(p);

    require(p, TOK_THEN);

    JStarStmt* thenBody = blockStmt(p);
    JStarStmt* elseBody = NULL;

    if(match(p, TOK_ELIF)) {
        int line = p->peek.line;
        advance(p);
        skipNewLines(p);
        elseBody = ifBody(p, line);
    } else if(match(p, TOK_ELSE)) {
        advance(p);
        elseBody = blockStmt(p);
    }

    return jsrIfStmt(line, cond, thenBody, elseBody);
}

static JStarStmt* ifStmt(Parser* p) {
    int line = p->peek.line;
    advance(p);
    skipNewLines(p);

    JStarStmt* ifStmt = ifBody(p, line);
    require(p, TOK_END);

    return ifStmt;
}

static JStarStmt* whileStmt(Parser* p) {
    int line = p->peek.line;
    advance(p);
    skipNewLines(p);

    JStarExpr* cond = expression(p, true);
    skipNewLines(p);

    require(p, TOK_DO);
    JStarStmt* body = blockStmt(p);
    require(p, TOK_END);

    return jsrWhileStmt(line, cond, body);
}

static JStarStmt* varDecl(Parser* p) {
    int line = p->peek.line;
    advance(p);
    skipNewLines(p);

    bool isUnpack = false;
    Vector identifiers = vecNew();

    do {
        JStarTok id = require(p, TOK_IDENTIFIER);
        vecPush(&identifiers, jsrNewIdentifier(id.length, id.lexeme));

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

    return jsrVarDecl(line, isUnpack, &identifiers, init);
}

static JStarStmt* forEach(Parser* p, JStarStmt* var, int line) {
    if(var->as.varDecl.init != NULL) {
        error(p, "Variable declaration in foreach cannot have initializer.");
    }

    advance(p);
    skipNewLines(p);

    JStarExpr* e = expression(p, true);
    skipNewLines(p);

    require(p, TOK_DO);
    JStarStmt* body = blockStmt(p);
    require(p, TOK_END);

    return jsrForEachStmt(line, var, e, body);
}

static JStarStmt* forStmt(Parser* p) {
    int line = p->peek.line;
    advance(p);
    skipNewLines(p);

    JStarStmt* init = NULL;
    if(!match(p, TOK_SEMICOLON)) {
        if(match(p, TOK_VAR)) {
            init = varDecl(p);
            skipNewLines(p);
            if(match(p, TOK_IN)) {
                return forEach(p, init, line);
            }
        } else {
            JStarExpr* e = expression(p, true);
            init = jsrExprStmt(e->line, e);
        }
    }

    skipNewLines(p);
    require(p, TOK_SEMICOLON);
    skipNewLines(p);

    JStarExpr* cond = NULL;
    if(!match(p, TOK_SEMICOLON)) cond = expression(p, true);

    skipNewLines(p);
    require(p, TOK_SEMICOLON);
    skipNewLines(p);

    JStarExpr* act = NULL;
    if(!match(p, TOK_DO)) act = expression(p, true);

    skipNewLines(p);
    require(p, TOK_DO);

    JStarStmt* body = blockStmt(p);
    require(p, TOK_END);

    return jsrForStmt(line, init, cond, act, body);
}

static JStarStmt* returnStmt(Parser* p) {
    int line = p->peek.line;
    advance(p);

    JStarExpr* e = NULL;
    if(!isStatementEnd(&p->peek)) {
        e = expression(p, true);
    }

    requireStmtEnd(p);
    return jsrReturnStmt(line, e);
}

static JStarStmt* importStmt(Parser* p) {
    int line = p->peek.line;
    advance(p);
    skipNewLines(p);

    Vector modules = vecNew();

    for(;;) {
        JStarTok name = require(p, TOK_IDENTIFIER);
        vecPush(&modules, jsrNewIdentifier(name.length, name.lexeme));
        if(!match(p, TOK_DOT)) break;
        advance(p);
        skipNewLines(p);
    }

    JStarTok asName = {0};
    Vector importNames = vecNew();

    if(match(p, TOK_FOR)) {
        advance(p);
        skipNewLines(p);

        if(match(p, TOK_MULT)) {
            JStarTok all = advance(p);
            vecPush(&importNames, jsrNewIdentifier(all.length, all.lexeme));
        } else {
            for(;;) {
                JStarTok name = require(p, TOK_IDENTIFIER);
                vecPush(&importNames, jsrNewIdentifier(name.length, name.lexeme));
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
    return jsrImportStmt(line, &modules, &importNames, &asName);
}

static JStarStmt* tryStmt(Parser* p) {
    int line = p->peek.line;
    advance(p);

    JStarStmt* tryBlock = blockStmt(p);
    Vector excs = vecNew();
    JStarStmt* ensure = NULL;

    while(match(p, TOK_EXCEPT)) {
        int excLine = p->peek.line;
        advance(p);
        skipNewLines(p);

        JStarExpr* cls = expression(p, true);
        skipNewLines(p);

        JStarTok var = require(p, TOK_IDENTIFIER);

        JStarStmt* block = blockStmt(p);
        vecPush(&excs, jsrExceptStmt(excLine, cls, &var, block));
    }

    if(match(p, TOK_ENSURE)) {
        advance(p);
        ensure = blockStmt(p);
    }

    if(vecEmpty(&excs) && !ensure) {
        error(p, "Expected except or ensure clause");
    }

    require(p, TOK_END);
    return jsrTryStmt(line, tryBlock, &excs, ensure);
}

static JStarStmt* raiseStmt(Parser* p) {
    int line = p->peek.line;
    advance(p);
    skipNewLines(p);

    JStarExpr* exc = expression(p, true);
    requireStmtEnd(p);

    return jsrRaiseStmt(line, exc);
}

static JStarStmt* withStmt(Parser* p) {
    int line = p->peek.line;
    advance(p);
    skipNewLines(p);

    JStarExpr* e = expression(p, true);
    skipNewLines(p);

    JStarTok var = require(p, TOK_IDENTIFIER);
    JStarStmt* block = blockStmt(p);
    require(p, TOK_END);

    return jsrWithStmt(line, e, &var, block);
}

static JStarStmt* funcDecl(Parser* p) {
    int line = p->peek.line;
    advance(p);
    skipNewLines(p);

    JStarTok funcName = require(p, TOK_IDENTIFIER);
    skipNewLines(p);

    FormalArgs args = formalArgs(p, TOK_LPAREN, TOK_RPAREN);
    JStarStmt* body = blockStmt(p);
    require(p, TOK_END);

    return jsrFuncDecl(line, &funcName, &args.arguments, &args.defaults, args.isVararg, body);
}

static JStarStmt* nativeDecl(Parser* p) {
    int line = p->peek.line;
    advance(p);
    skipNewLines(p);

    JStarTok funcName = require(p, TOK_IDENTIFIER);
    skipNewLines(p);

    FormalArgs args = formalArgs(p, TOK_LPAREN, TOK_RPAREN);
    requireStmtEnd(p);

    return jsrNativeDecl(line, &funcName, &args.arguments, &args.defaults, args.isVararg);
}

static JStarStmt* classDecl(Parser* p) {
    int line = p->peek.line;
    advance(p);
    skipNewLines(p);

    JStarTok clsName = require(p, TOK_IDENTIFIER);
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

    Vector methods = vecNew();
    while(!match(p, TOK_END) && !match(p, TOK_EOF)) {
        switch(p->peek.type) {
        case TOK_NAT:
            vecPush(&methods, nativeDecl(p));
            break;
        case TOK_FUN:
            vecPush(&methods, funcDecl(p));
            break;
        default:
            error(p, "Expected function or native delcaration.");
            advance(p);
            break;
        }
        skipNewLines(p);
        if(p->panic) classSynchronize(p);
    }

    require(p, TOK_END);
    return jsrClassDecl(line, &clsName, sup, &methods);
}

static JStarExpr* parseAssignment(Parser* p, JStarExpr* l, bool parseTuple) {
    checkLvalue(p, l, p->peek.type);
    JStarTok assignToken = advance(p);
    JStarExpr* r = expression(p, parseTuple);

    if(isCompoundAssign(&assignToken)) {
        JStarTokType op = assignToOperator(assignToken.type);
        l = jsrCompundAssExpr(assignToken.line, op, l, r);
    } else {
        l = jsrAssignExpr(assignToken.line, l, r);
    }

    return l;
}

static JStarStmt* exprStmt(Parser* p) {
    JStarExpr* l = tupleLiteral(p);

    if(!isAssign(&p->peek) && !isCallExpression(l)) {
        error(p, "Syntax error.");
    }

    if(isAssign(&p->peek)) {
        l = parseAssignment(p, l, true);
    }

    return jsrExprStmt(l->line, l);
}

static JStarStmt* parseStmt(Parser* p) {
    int line = p->peek.line;

    switch(p->peek.type) {
    case TOK_CLASS:
        return classDecl(p);
    case TOK_NAT:
        return nativeDecl(p);
    case TOK_FUN:
        return funcDecl(p);
    case TOK_VAR: {
        JStarStmt* var = varDecl(p);
        requireStmtEnd(p);
        return var;
    }
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
        return jsrContinueStmt(line);
    case TOK_BREAK:
        advance(p);
        requireStmtEnd(p);
        return jsrBreakStmt(line);
    default:
        break;
    }

    JStarStmt* stmt = exprStmt(p);
    requireStmtEnd(p);
    return stmt;
}

static JStarStmt* parseProgram(Parser* p) {
    skipNewLines(p);

    Vector stmts = vecNew();
    while(!match(p, TOK_EOF)) {
        vecPush(&stmts, parseStmt(p));
        skipNewLines(p);
        if(p->panic) synchronize(p);
    }

    // Top level function doesn't have name or arguments, so pass them empty
    return jsrFuncDecl(0, &(JStarTok){0}, &(Vector){0}, &(Vector){0}, false,
                       jsrBlockStmt(0, &stmts));
}

// -----------------------------------------------------------------------------
// EXPRESSIONS PARSE
// -----------------------------------------------------------------------------

static JStarExpr* expressionLst(Parser* p, JStarTokType open, JStarTokType close) {
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
    return jsrExprList(line, &exprs);
}

static JStarExpr* parseTableLiteral(Parser* p) {
    int line = p->peek.line;
    advance(p);
    skipNewLines(p);

    Vector keyVals = vecNew();
    while(!match(p, TOK_RCURLY)) {
        JStarExpr* key;
        if(match(p, TOK_DOT)) {
            advance(p);
            skipNewLines(p);
            JStarTok id = require(p, TOK_IDENTIFIER);
            key = jsrStrLiteral(id.line, id.lexeme, id.length);
        } else {
            key = expression(p, false);
        }

        skipNewLines(p);
        require(p, TOK_COLON);
        skipNewLines(p);

        JStarExpr* val = expression(p, false);
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
    return jsrTableLiteral(line, jsrExprList(line, &keyVals));
}

static JStarExpr* parseSuperLiteral(Parser* p) {
    int line = p->peek.line;
    advance(p);

    JStarTok name = {0};
    JStarExpr* args = NULL;

    if(match(p, TOK_DOT)) {
        advance(p);
        skipNewLines(p);
        name = require(p, TOK_IDENTIFIER);
    }

    bool unpackArg = false;

    if(match(p, TOK_LPAREN)) {
        args = expressionLst(p, TOK_LPAREN, TOK_RPAREN);
        if(match(p, TOK_VARARG)) {
            advance(p);
            unpackArg = true;
        }
    } else if(match(p, TOK_LCURLY)) {
        Vector tableCallArgs = vecNew();
        vecPush(&tableCallArgs, parseTableLiteral(p));
        args = jsrExprList(line, &tableCallArgs);
    }

    return jsrSuperLiteral(line, &name, args, unpackArg);
}

static JStarExpr* literal(Parser* p) {
    int line = p->peek.line;
    JStarTok* tok = &p->peek;

    switch(tok->type) {
    case TOK_SUPER:
        return parseSuperLiteral(p);
    case TOK_LCURLY:
        return parseTableLiteral(p);
    case TOK_TRUE:
        return advance(p), jsrBoolLiteral(line, true);
    case TOK_FALSE:
        return advance(p), jsrBoolLiteral(line, false);
    case TOK_NULL:
        return advance(p), jsrNullLiteral(line);
    case TOK_NUMBER: {
        JStarExpr* e = jsrNumLiteral(line, strtod(tok->lexeme, NULL));
        advance(p);
        return e;
    }
    case TOK_IDENTIFIER: {
        JStarExpr* e = jsrVarLiteral(line, tok->lexeme, tok->length);
        advance(p);
        return e;
    }
    case TOK_STRING: {
        JStarExpr* e = jsrStrLiteral(line, tok->lexeme + 1, tok->length - 2);
        advance(p);
        return e;
    }
    case TOK_LSQUARE: {
        JStarExpr* exprs = expressionLst(p, TOK_LSQUARE, TOK_RSQUARE);
        return jsrArrLiteral(line, exprs);
    }
    case TOK_LPAREN: {
        advance(p);
        skipNewLines(p);

        if(match(p, TOK_RPAREN)) {
            advance(p);
            return jsrTupleLiteral(line, jsrExprList(line, &(Vector){0}));
        }

        JStarExpr* e = expression(p, true);

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

    // Return dummy expression to avoid NULL
    return jsrNullLiteral(line);
}

static JStarExpr* postfixExpr(Parser* p) {
    JStarExpr* lit = literal(p);

    JStarTokType tokens[] = {TOK_LPAREN, TOK_LCURLY, TOK_DOT, TOK_LSQUARE};
    while(matchAny(p, tokens, sizeof(tokens) / sizeof(JStarTokType))) {
        int line = p->peek.line;
        switch(p->peek.type) {
        case TOK_DOT: {
            advance(p);
            skipNewLines(p);
            JStarTok attr = require(p, TOK_IDENTIFIER);
            lit = jsrAccessExpr(line, lit, attr.lexeme, attr.length);
            break;
        }
        case TOK_LCURLY: {
            Vector tableCallArgs = vecNew();
            vecPush(&tableCallArgs, parseTableLiteral(p));
            JStarExpr* args = jsrExprList(line, &tableCallArgs);
            lit = jsrCallExpr(line, lit, args, false);
            break;
        }
        case TOK_LPAREN: {
            JStarExpr* args = expressionLst(p, TOK_LPAREN, TOK_RPAREN);
            bool unpackArg = false;
            if(match(p, TOK_VARARG)) {
                advance(p);
                unpackArg = true;
            }
            lit = jsrCallExpr(line, lit, args, unpackArg);
            break;
        }
        case TOK_LSQUARE: {
            require(p, TOK_LSQUARE);
            skipNewLines(p);
            lit = jsrArrayAccExpr(line, lit, expression(p, true));
            skipNewLines(p);
            require(p, TOK_RSQUARE);
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
        base = jsrPowExpr(powOp.line, base, exp);
    }

    return base;
}

static JStarExpr* unaryExpr(Parser* p) {
    JStarTokType tokens[] = {TOK_BANG, TOK_MINUS, TOK_HASH, TOK_HASH_HASH};

    if(matchAny(p, tokens, sizeof(tokens) / sizeof(JStarTokType))) {
        JStarTok op = advance(p);
        skipNewLines(p);
        return jsrUnaryExpr(op.line, op.type, unaryExpr(p));
    }

    return powExpr(p);
}

static JStarExpr* parseBinary(Parser* p, JStarTokType* tokens, int count,
                              JStarExpr* (*operand)(Parser*)) {
    JStarExpr* l = (*operand)(p);

    while(matchAny(p, tokens, count)) {
        JStarTok op = advance(p);
        skipNewLines(p);
        JStarExpr* r = (*operand)(p);
        l = jsrBinaryExpr(op.line, op.type, l, r);
    }

    return l;
}

static JStarExpr* multiplicativeExpr(Parser* p) {
    JStarTokType tokens[] = {TOK_MULT, TOK_DIV, TOK_MOD};
    return parseBinary(p, tokens, sizeof(tokens) / sizeof(JStarTokType), unaryExpr);
}

static JStarExpr* additiveExpr(Parser* p) {
    JStarTokType tokens[] = {TOK_PLUS, TOK_MINUS};
    return parseBinary(p, tokens, sizeof(tokens) / sizeof(JStarTokType), multiplicativeExpr);
}

static JStarExpr* relationalExpr(Parser* p) {
    JStarTokType tokens[] = {TOK_GT, TOK_GE, TOK_LT, TOK_LE, TOK_IS};
    return parseBinary(p, tokens, sizeof(tokens) / sizeof(JStarTokType), additiveExpr);
}

static JStarExpr* equalityExpr(Parser* p) {
    JStarTokType tokens[] = {TOK_EQUAL_EQUAL, TOK_BANG_EQ};
    return parseBinary(p, tokens, sizeof(tokens) / sizeof(JStarTokType), relationalExpr);
}

static JStarExpr* logicAndExpr(Parser* p) {
    JStarTokType tokens[] = {TOK_AND};
    return parseBinary(p, tokens, sizeof(tokens) / sizeof(JStarTokType), equalityExpr);
}

static JStarExpr* logicOrExpr(Parser* p) {
    JStarTokType tokens[] = {TOK_OR};
    return parseBinary(p, tokens, sizeof(tokens) / sizeof(JStarTokType), logicAndExpr);
}

static JStarExpr* ternaryExpr(Parser* p) {
    int line = p->peek.line;
    JStarExpr* expr = logicOrExpr(p);

    if(match(p, TOK_IF)) {
        advance(p);
        skipNewLines(p);

        JStarExpr* cond = ternaryExpr(p);

        require(p, TOK_ELSE);
        skipNewLines(p);

        JStarExpr* elseExpr = ternaryExpr(p);
        return jsrTernaryExpr(line, cond, expr, elseExpr);
    }

    return expr;
}

static JStarExpr* funcLiteral(Parser* p) {
    if(match(p, TOK_FUN)) {
        int line = p->peek.line;
        require(p, TOK_FUN);
        skipNewLines(p);

        FormalArgs args = formalArgs(p, TOK_LPAREN, TOK_RPAREN);
        JStarStmt* body = blockStmt(p);
        require(p, TOK_END);

        return jsrFuncLiteral(line, &args.arguments, &args.defaults, args.isVararg, body);
    }
    if(match(p, TOK_PIPE)) {
        int line = p->peek.line;
        FormalArgs args = formalArgs(p, TOK_PIPE, TOK_PIPE);
        skipNewLines(p);

        require(p, TOK_ARROW);
        skipNewLines(p);

        JStarExpr* e = expression(p, false);
        Vector anonFuncStmts = vecNew();
        vecPush(&anonFuncStmts, jsrReturnStmt(line, e));
        JStarStmt* body = jsrBlockStmt(line, &anonFuncStmts);

        return jsrFuncLiteral(line, &args.arguments, &args.defaults, args.isVararg, body);
    }
    return ternaryExpr(p);
}

static JStarExpr* tupleLiteral(Parser* p) {
    int line = p->peek.line;
    JStarExpr* e = funcLiteral(p);

    if(match(p, TOK_COMMA)) {
        Vector exprs = vecNew();
        vecPush(&exprs, e);

        while(match(p, TOK_COMMA)) {
            advance(p);
            if(!isExpressionStart(&p->peek)) break;
            vecPush(&exprs, funcLiteral(p));
        }

        e = jsrTupleLiteral(line, jsrExprList(line, &exprs));
    }

    return e;
}

static JStarExpr* expression(Parser* p, bool parseTuple) {
    JStarExpr* l = parseTuple ? tupleLiteral(p) : funcLiteral(p);

    if(isAssign(&p->peek)) {
        return parseAssignment(p, l, parseTuple);
    }

    return l;
}

// -----------------------------------------------------------------------------
// API
// -----------------------------------------------------------------------------

JStarStmt* jsrParse(const char* path, const char* src, ParseErrorCB errorCallback) {
    Parser p;
    initParser(&p, path, src, errorCallback);

    JStarStmt* program = parseProgram(&p);
    skipNewLines(&p);

    if(!match(&p, TOK_EOF)) {
        error(&p, "Unexpected token.");
    }

    if(p.hadError) {
        jsrStmtFree(program);
        return NULL;
    }

    return program;
}

JStarExpr* jsrParseExpression(const char* path, const char* src, ParseErrorCB errorCallback) {
    Parser p;
    initParser(&p, path, src, errorCallback);

    JStarExpr* expr = expression(&p, true);
    skipNewLines(&p);

    if(!match(&p, TOK_EOF)) {
        error(&p, "Unexpected token.");
    }

    if(p.hadError) {
        jsrExprFree(expr);
        return NULL;
    }

    return expr;
}
