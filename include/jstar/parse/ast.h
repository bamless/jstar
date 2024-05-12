#ifndef JSTAR_AST_H
#define JSTAR_AST_H

#include <stdbool.h>
#include <stdlib.h>

#include "lex.h"
#include "vector.h"

typedef struct JStarExpr JStarExpr;
typedef struct JStarStmt JStarStmt;
typedef struct JStarDecl JStarDecl;

typedef struct JStarIdentifier {
    size_t length;
    const char* name;
} JStarIdentifier;

typedef enum JStarExprType {
    JSR_BINARY,
    JSR_UNARY,
    JSR_ASSIGN,
    JSR_NUMBER,
    JSR_BOOL,
    JSR_STRING,
    JSR_VAR,
    JSR_NULL,
    JSR_EXPR_LST,
    JSR_CALL,
    JSR_POWER,
    JSR_SUPER,
    JSR_ACCESS,
    JSR_YIELD,
    JSR_ARRAY,
    JSR_TUPLE,
    JSR_TABLE,
    JSR_ARR_ACCESS,
    JSR_TERNARY,
    JSR_COMPUND_ASS,
    JSR_FUNC_LIT,
    JSR_SPREAD,
} JStarExprType;

struct JStarExpr {
    int line;
    JStarExprType type;
    union {
        struct {
            JStarTokType op;
            JStarExpr *left, *right;
        } binary;
        struct {
            JStarTokType op;
            JStarExpr* operand;
        } unary;
        struct {
            JStarExpr* expr;
        } spread;
        struct {
            JStarExpr *lval, *rval;
        } assign;
        struct {
            JStarTokType op;
            JStarExpr *lval, *rval;
        } compound;
        struct {
            size_t length;
            const char* str;
        } string;
        struct {
            JStarIdentifier id;
        } var;
        struct {
            JStarExpr *callee, *args;
        } call;
        struct {
            JStarExpr *base, *exp;
        } pow;
        struct {
            JStarExpr* left;
            JStarIdentifier id;
        } access;
        struct {
            JStarExpr* left;
            JStarExpr* index;
        } arrayAccess;
        struct {
            JStarExpr* expr;
        } yield;
        struct {
            JStarExpr* exprs;
        } array;
        struct {
            JStarExpr* exprs;
        } tuple;
        struct {
            JStarExpr* keyVals;
        } table;
        struct {
            JStarExpr* cond;
            JStarExpr* thenExpr;
            JStarExpr* elseExpr;
        } ternary;
        struct {
            JStarStmt* func;
        } funLit;
        struct {
            JStarIdentifier name;
            JStarExpr* args;
        } sup;
        double num;
        bool boolean;
        ext_vector(JStarExpr*) list;
    } as;
};

typedef enum JStarStmtType {
    JSR_IF,
    JSR_FOR,
    JSR_WHILE,
    JSR_FOREACH,
    JSR_BLOCK,
    JSR_RETURN,
    JSR_EXPR_STMT,
    JSR_VARDECL,
    JSR_FUNCDECL,
    JSR_NATIVEDECL,
    JSR_CLASSDECL,
    JSR_IMPORT,
    JSR_TRY,
    JSR_EXCEPT,
    JSR_RAISE,
    JSR_WITH,
    JSR_CONTINUE,
    JSR_BREAK
} JStarStmtType;

typedef struct FormalArg {
    enum {
        SIMPLE,
        UNPACK
    } type;
    union {
        JStarIdentifier simple;
        ext_vector(JStarIdentifier) unpack;
    } as;
} FormalArg;

typedef struct FormalArgs {
    ext_vector(FormalArg) args;
    ext_vector(JStarExpr*) defaults;
    JStarIdentifier vararg;
} FormalArgs;

struct JStarDecl {
    ext_vector(JStarExpr*) decorators;
    bool isStatic;
    union {
        struct {
            bool isUnpack;
            ext_vector(JStarIdentifier) ids;
            JStarExpr* init;
        } var;
        struct {
            JStarIdentifier id;
            FormalArgs formalArgs;
            bool isGenerator;
            JStarStmt* body;
        } fun;
        struct {
            JStarIdentifier id;
            FormalArgs formalArgs;
        } native;
        struct {
            JStarIdentifier id;
            JStarExpr* sup;
            ext_vector(JStarStmt*) methods;
        } cls;
    } as;
};

struct JStarStmt {
    int line;
    JStarStmtType type;
    // Declarations
    union {
        // Control flow statements
        struct {
            JStarExpr* cond;
            JStarStmt *thenStmt, *elseStmt;
        } ifStmt;
        struct {
            JStarStmt* init;
            JStarExpr *cond, *act;
            JStarStmt* body;
        } forStmt;
        struct {
            JStarStmt* var;
            JStarExpr* iterable;
            JStarStmt* body;
        } forEach;
        struct {
            JStarExpr* cond;
            JStarStmt* body;
        } whileStmt;
        struct {
            JStarExpr* e;
        } returnStmt;
        struct {
            ext_vector(JStarStmt*) stmts;
        } blockStmt;
        struct {
            ext_vector(JStarIdentifier) modules;
            JStarIdentifier as;
            ext_vector(JStarIdentifier) names;
        } importStmt;
        struct {
            JStarStmt* block;
            ext_vector(JStarStmt*) excs;
            JStarStmt* ensure;
        } tryStmt;
        struct {
            JStarExpr* cls;
            JStarIdentifier var;
            JStarStmt* block;
        } excStmt;
        struct {
            JStarExpr* exc;
        } raiseStmt;
        struct {
            JStarExpr* e;
            JStarIdentifier var;
            JStarStmt* block;
        } withStmt;
        JStarExpr* exprStmt;
        JStarDecl decl;
    } as;
};

// -----------------------------------------------------------------------------
// IDENTIFIER FUNCTIONS
// -----------------------------------------------------------------------------

JSTAR_API bool jsrIdentifierEq(const JStarIdentifier* id1, const JStarIdentifier* id2);

// -----------------------------------------------------------------------------
// EXPRESSION NODES
// -----------------------------------------------------------------------------

JSTAR_API JStarExpr* jsrFuncLiteral(int line, const FormalArgs* args, bool isGenerator, JStarStmt* body);
JSTAR_API JStarExpr* jsrTernaryExpr(int line, JStarExpr* cond, JStarExpr* thenExpr, JStarExpr* elseExpr);
JSTAR_API JStarExpr* jsrCompundAssExpr(int line, JStarTokType op, JStarExpr* lval, JStarExpr* rval);
JSTAR_API JStarExpr* jsrAccessExpr(int line, JStarExpr* left, const char* name, size_t length);
JSTAR_API JStarExpr* jsrSuperLiteral(int line, JStarTok* name, JStarExpr* args);
JSTAR_API JStarExpr* jsrCallExpr(int line, JStarExpr* callee, JStarExpr* args);
JSTAR_API JStarExpr* jsrVarLiteral(int line, const char* str, size_t len);
JSTAR_API JStarExpr* jsrStrLiteral(int line, const char* str, size_t len);
JSTAR_API JStarExpr* jsrArrayAccExpr(int line, JStarExpr* left, JStarExpr* index);
JSTAR_API JStarExpr* jsrBinaryExpr(int line, JStarTokType op, JStarExpr* l, JStarExpr* r);
JSTAR_API JStarExpr* jsrUnaryExpr(int line, JStarTokType op, JStarExpr* operand);
JSTAR_API JStarExpr* jsrAssignExpr(int line, JStarExpr* lval, JStarExpr* rval);
JSTAR_API JStarExpr* jsrPowExpr(int line, JStarExpr* base, JStarExpr* exp);
JSTAR_API JStarExpr* jsrTableLiteral(int line, JStarExpr* keyVals);
JSTAR_API JStarExpr* jsrSpreadExpr(int line, JStarExpr* expr);
JSTAR_API JStarExpr* jsrExprList(int line, ext_vector(JStarExpr*) exprs);
JSTAR_API JStarExpr* jsrBoolLiteral(int line, bool boolean);
JSTAR_API JStarExpr* jsrTupleLiteral(int line, JStarExpr* exprs);
JSTAR_API JStarExpr* jsrArrLiteral(int line, JStarExpr* exprs);
JSTAR_API JStarExpr* jsrYieldExpr(int line, JStarExpr* expr);
JSTAR_API JStarExpr* jsrNumLiteral(int line, double num);
JSTAR_API JStarExpr* jsrNullLiteral(int line);
JSTAR_API void jsrExprFree(JStarExpr* e);

// -----------------------------------------------------------------------------
// STATEMENT NODES
// -----------------------------------------------------------------------------

JSTAR_API JStarStmt* jsrFuncDecl(int line, const JStarIdentifier* name, const FormalArgs* args, bool isGenerator, JStarStmt* body);
JSTAR_API JStarStmt* jsrNativeDecl(int line, const JStarIdentifier* name, const FormalArgs* args);
JSTAR_API JStarStmt* jsrForStmt(int line, JStarStmt* init, JStarExpr* cond, JStarExpr* act, JStarStmt* body);
JSTAR_API JStarStmt* jsrClassDecl(int line, const JStarIdentifier* clsName, JStarExpr* sup, ext_vector(JStarStmt*) methods);
JSTAR_API JStarStmt* jsrImportStmt(int line, ext_vector(JStarIdentifier) modules, ext_vector(JStarIdentifier) names, const JStarIdentifier* as);
JSTAR_API JStarStmt* jsrVarDecl(int line, bool isUnpack, ext_vector(JStarIdentifier) ids, JStarExpr* init);
JSTAR_API JStarStmt* jsrTryStmt(int line, JStarStmt* blck, ext_vector(JStarStmt*) excs, JStarStmt* ensure);
JSTAR_API JStarStmt* jsrIfStmt(int line, JStarExpr* cond, JStarStmt* thenStmt, JStarStmt* elseStmt);
JSTAR_API JStarStmt* jsrForEachStmt(int line, JStarStmt* varDecl, JStarExpr* iter, JStarStmt* body);
JSTAR_API JStarStmt* jsrExceptStmt(int line, JStarExpr* cls, const JStarIdentifier* varName, JStarStmt* block);
JSTAR_API JStarStmt* jsrWithStmt(int line, JStarExpr* e, const JStarIdentifier* varName, JStarStmt* block);
JSTAR_API JStarStmt* jsrWhileStmt(int line, JStarExpr* cond, JStarStmt* body);
JSTAR_API JStarStmt* jsrBlockStmt(int line, ext_vector(JStarStmt*) list);
JSTAR_API JStarStmt* jsrReturnStmt(int line, JStarExpr* e);
JSTAR_API JStarStmt* jsrRaiseStmt(int line, JStarExpr* e);
JSTAR_API JStarStmt* jsrExprStmt(int line, JStarExpr* e);
JSTAR_API JStarStmt* jsrContinueStmt(int line);
JSTAR_API JStarStmt* jsrBreakStmt(int line);
JSTAR_API void jsrStmtFree(JStarStmt* s);

#endif
