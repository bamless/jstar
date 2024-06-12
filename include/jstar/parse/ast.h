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

// -----------------------------------------------------------------------------
// EXPRESSION NODES
// -----------------------------------------------------------------------------

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
    JSR_PROPERTY_ACCESS,
    JSR_YIELD,
    JSR_LIST,
    JSR_TUPLE,
    JSR_TABLE,
    JSR_INDEX,
    JSR_TERNARY,
    JSR_COMPOUND_ASSIGN,
    JSR_FUN_LIT,
    JSR_SPREAD,
} JStarExprType;

typedef struct JStarBinaryExpr {
    JStarTokType op;
    JStarExpr *left, *right;
} JStarBinaryExpr;

typedef struct JStarUnaryExpr {
    JStarTokType op;
    JStarExpr* operand;
} JStarUnaryExpr;

typedef struct JStarSpreadExpr {
    JStarExpr* expr;
} JStarSpreadExpr;

typedef struct JStarAssignExpr {
    JStarExpr *lval, *rval;
} JStarAssignExpr;

typedef struct JStarCompoundAssignExpr {
    JStarTokType op;
    JStarExpr *lval, *rval;
} JStarCompoundAssignExpr;

typedef struct JStarStringLiteralExpr {
    size_t length;
    const char* str;
} JStarStringLiteralExpr;

typedef struct JStarVarLiteralExpr {
    JStarIdentifier id;
} JStarVarLiteralExpr;

typedef struct JStarCallExpr {
    JStarExpr *callee, *args;
} JStarCallExpr;

typedef struct JStarPowExpr {
    JStarExpr *base, *exp;
} JStarPowExpr;

typedef struct JStarPropertyAccessExpr {
    JStarExpr* left;
    JStarIdentifier id;
} JStarPropertyAccessExpr;

typedef struct JStarIndexExpr {
    JStarExpr* left;
    JStarExpr* index;
} JStarIndexExpr;

typedef struct JStarYieldExpr {
    JStarExpr* expr;
} JStarYieldExpr;

typedef struct JStarListLiteralExpr {
    JStarExpr* exprs;
} JStarListLiteralExpr;

typedef struct JStarTupleLiteralExpr {
    JStarExpr* exprs;
} JStarTupleLiteralExpr;

typedef struct JStarTableLiteralExpr {
    JStarExpr* keyVals;
} JStarTableLiteralExpr;

typedef struct JStarTernaryExpr {
    JStarExpr* cond;
    JStarExpr* thenExpr;
    JStarExpr* elseExpr;
} JStarTernaryExpr;

typedef struct JStarFunLiteralExpr {
    JStarStmt* func;
} JStarFunLiteralExpr;

typedef struct JStarSuperLiteralExpr {
    JStarIdentifier name;
    JStarExpr* args;
} JStarSuperLiteralExpr;

struct JStarExpr {
    int line;
    JStarExprType type;
    union {
        JStarBinaryExpr binary;
        JStarUnaryExpr unary;
        JStarSpreadExpr spread;
        JStarAssignExpr assign;
        JStarCompoundAssignExpr compoundAssign;
        JStarCallExpr call;
        JStarPowExpr pow;
        JStarPropertyAccessExpr propertyAccess;
        JStarIndexExpr index;
        JStarYieldExpr yield;
        JStarTernaryExpr ternary;
        JStarFunLiteralExpr funLit;
        JStarSuperLiteralExpr sup;
        JStarStringLiteralExpr stringLiteral;
        JStarVarLiteralExpr varLiteral;
        JStarListLiteralExpr listLiteral;
        JStarTupleLiteralExpr tupleLiteral;
        JStarTableLiteralExpr tableLiteral;
        ext_vector(JStarExpr*) exprList;
        double num;
        bool boolean;
    } as;
};

// -----------------------------------------------------------------------------
// DECLARATION NODES
// -----------------------------------------------------------------------------

typedef struct JStarFormalArg {
    enum { SIMPLE, UNPACK } type;
    union {
        JStarIdentifier simple;
        ext_vector(JStarIdentifier) unpack;
    } as;
} JStarFormalArg;

typedef struct JStarFormalArgs {
    ext_vector(JStarFormalArg) args;
    ext_vector(JStarExpr*) defaults;
    JStarIdentifier vararg;
} JStarFormalArgs;

typedef struct JStarVarDecl {
    bool isUnpack;
    ext_vector(JStarIdentifier) ids;
    JStarExpr* init;
} JStarVarDecl;

typedef struct JStarFunDecl {
    JStarIdentifier id;
    JStarFormalArgs formalArgs;
    bool isGenerator;
    JStarStmt* body;
} JStarFunDecl;

typedef struct JStarNativeDecl {
    JStarIdentifier id;
    JStarFormalArgs formalArgs;
} JStarNativeDecl;

typedef struct JStarClassDecl {
    JStarIdentifier id;
    JStarExpr* sup;
    ext_vector(JStarStmt*) methods;
} JStarClassDecl;

struct JStarDecl {
    bool isStatic;
    ext_vector(JStarExpr*) decorators;
    union {
        JStarVarDecl var;
        JStarFunDecl fun;
        JStarNativeDecl native;
        JStarClassDecl cls;
    } as;
};

// -----------------------------------------------------------------------------
// STATEMENT NODES
// -----------------------------------------------------------------------------

typedef enum JStarStmtType {
    JSR_VARDECL,
    JSR_FUNCDECL,
    JSR_NATIVEDECL,
    JSR_CLASSDECL,
    JSR_IF,
    JSR_FOR,
    JSR_WHILE,
    JSR_FOREACH,
    JSR_BLOCK,
    JSR_RETURN,
    JSR_EXPR_STMT,
    JSR_IMPORT,
    JSR_TRY,
    JSR_EXCEPT,
    JSR_RAISE,
    JSR_WITH,
    JSR_CONTINUE,
    JSR_BREAK
} JStarStmtType;

typedef struct JStarIfStmt {
    JStarExpr* cond;
    JStarStmt *thenStmt, *elseStmt;
} JStarIfStmt;

typedef struct JStarForStmt {
    JStarStmt* init;
    JStarExpr *cond, *act;
    JStarStmt* body;
} JStarForStmt;

typedef struct JStarForEachStmt {
    JStarStmt* var;
    JStarExpr* iterable;
    JStarStmt* body;
} JStarForEachStmt;

typedef struct JStarWhileStmt {
    JStarExpr* cond;
    JStarStmt* body;
} JStarWhileStmt;

typedef struct JStarBlockStmt {
    ext_vector(JStarStmt*) stmts;
} JStarBlockStmt;

typedef struct JStarImportStmt {
    ext_vector(JStarIdentifier) modules;
    JStarIdentifier as;
    ext_vector(JStarIdentifier) names;
} JStarImportStmt;

typedef struct JStarTryStmt {
    JStarStmt* block;
    ext_vector(JStarStmt*) excs;
    JStarStmt* ensure;
} JStarTryStmt;

typedef struct JStarExceptStmt {
    JStarExpr* cls;
    JStarIdentifier var;
    JStarStmt* block;
} JStarExceptStmt;

typedef struct JStarRaiseStmt {
    JStarExpr* exc;
} JStarRaiseStmt;

typedef struct JStarWithStmt {
    JStarExpr* e;
    JStarIdentifier var;
    JStarStmt* block;
} JStarWithStmt;

typedef struct JStarStmtList {
    ext_vector(JStarStmt*) stmts;
} JStarStmtList;

typedef struct JStarReturnStmt {
    JStarExpr* e;
} JStarReturnStmt;

struct JStarStmt {
    int line;
    JStarStmtType type;
    union {
        JStarIfStmt ifStmt;
        JStarForStmt forStmt;
        JStarForEachStmt forEach;
        JStarWhileStmt whileStmt;
        JStarBlockStmt blockStmt;
        JStarReturnStmt returnStmt;
        JStarImportStmt importStmt;
        JStarTryStmt tryStmt;
        JStarExceptStmt excStmt;
        JStarRaiseStmt raiseStmt;
        JStarWithStmt withStmt;
        JStarDecl decl;
        JStarExpr* exprStmt;
    } as;
};

// -----------------------------------------------------------------------------
// IDENTIFIER FUNCTIONS
// -----------------------------------------------------------------------------

JSTAR_API bool jsrIdentifierEq(const JStarIdentifier* id1, const JStarIdentifier* id2);

// -----------------------------------------------------------------------------
// EXPRESSION NODES
// -----------------------------------------------------------------------------

JSTAR_API JStarExpr* jsrFunLiteral(int line, const JStarFormalArgs* args, bool isGenerator,
                                   JStarStmt* body);
JSTAR_API JStarExpr* jsrTernaryExpr(int line, JStarExpr* cond, JStarExpr* thenExpr,
                                    JStarExpr* elseExpr);
JSTAR_API JStarExpr* jsrCompundAssignExpr(int line, JStarTokType op, JStarExpr* lval,
                                          JStarExpr* rval);
JSTAR_API JStarExpr* jsrPropertyAccessExpr(int line, JStarExpr* left, const char* name,
                                           size_t length);
JSTAR_API JStarExpr* jsrSuperLiteral(int line, JStarTok* name, JStarExpr* args);
JSTAR_API JStarExpr* jsrCallExpr(int line, JStarExpr* callee, JStarExpr* args);
JSTAR_API JStarExpr* jsrVarLiteral(int line, const char* str, size_t len);
JSTAR_API JStarExpr* jsrStrLiteral(int line, const char* str, size_t len);
JSTAR_API JStarExpr* jsrIndexExpr(int line, JStarExpr* left, JStarExpr* index);
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

JSTAR_API JStarStmt* jsrFuncDecl(int line, const JStarIdentifier* name, const JStarFormalArgs* args,
                                 bool isGenerator, JStarStmt* body);
JSTAR_API JStarStmt* jsrNativeDecl(int line, const JStarIdentifier* name, const JStarFormalArgs* args);
JSTAR_API JStarStmt* jsrForStmt(int line, JStarStmt* init, JStarExpr* cond, JStarExpr* act,
                                JStarStmt* body);
JSTAR_API JStarStmt* jsrClassDecl(int line, const JStarIdentifier* clsName, JStarExpr* sup,
                                  ext_vector(JStarStmt*) methods);
JSTAR_API JStarStmt* jsrImportStmt(int line, ext_vector(JStarIdentifier) modules,
                                   ext_vector(JStarIdentifier) names, const JStarIdentifier* as);
JSTAR_API JStarStmt* jsrVarDecl(int line, bool isUnpack, ext_vector(JStarIdentifier) ids,
                                JStarExpr* init);
JSTAR_API JStarStmt* jsrTryStmt(int line, JStarStmt* blck, ext_vector(JStarStmt*) excs,
                                JStarStmt* ensure);
JSTAR_API JStarStmt* jsrIfStmt(int line, JStarExpr* cond, JStarStmt* thenStmt, JStarStmt* elseStmt);
JSTAR_API JStarStmt* jsrForEachStmt(int line, JStarStmt* varDecl, JStarExpr* iter, JStarStmt* body);
JSTAR_API JStarStmt* jsrExceptStmt(int line, JStarExpr* cls, const JStarIdentifier* varName,
                                   JStarStmt* block);
JSTAR_API JStarStmt* jsrWithStmt(int line, JStarExpr* e, const JStarIdentifier* varName,
                                 JStarStmt* block);
JSTAR_API JStarStmt* jsrWhileStmt(int line, JStarExpr* cond, JStarStmt* body);
JSTAR_API JStarStmt* jsrBlockStmt(int line, ext_vector(JStarStmt*) list);
JSTAR_API JStarStmt* jsrReturnStmt(int line, JStarExpr* e);
JSTAR_API JStarStmt* jsrRaiseStmt(int line, JStarExpr* e);
JSTAR_API JStarStmt* jsrExprStmt(int line, JStarExpr* e);
JSTAR_API JStarStmt* jsrContinueStmt(int line);
JSTAR_API JStarStmt* jsrBreakStmt(int line);
JSTAR_API void jsrStmtFree(JStarStmt* s);

#endif
