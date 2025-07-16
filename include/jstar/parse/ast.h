#ifndef JSTAR_AST_H
#define JSTAR_AST_H

#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>

#include "jstar/conf.h"
#include "lex.h"

typedef struct JStarExpr JStarExpr;
typedef struct JStarStmt JStarStmt;
typedef struct JStarDecl JStarDecl;

typedef struct {
    size_t length;
    const char* name;
} JStarIdentifier;

typedef struct {
    JStarIdentifier* items;
    size_t count, capacity;
} JStarIdentifiers;

typedef struct {
    JStarExpr** items;
    size_t count, capacity;
} JStarExprs;

typedef struct {
    JStarStmt** items;
    size_t count, capacity;
} JStarStmts;

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
    JStarLoc loc;
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
        JStarExprs exprList;
        double num;
        bool boolean;
    } as;
};

// -----------------------------------------------------------------------------
// DECLARATION NODES
// -----------------------------------------------------------------------------

typedef struct JStarFormalArg {
    enum { SIMPLE, UNPACK } type;
    JStarLoc loc;
    union {
        JStarIdentifier simple;
        JStarIdentifiers unpack;
    } as;
} JStarFormalArg;

typedef struct {
    JStarFormalArg* items;
    size_t count, capacity;
} JStarFormalArgs;

typedef struct JStarFormalArgs {
    JStarFormalArgs args;
    JStarExprs defaults;
    JStarIdentifier vararg;
} JStarFormalArgsList;

typedef struct JStarVarDecl {
    bool isUnpack;
    JStarIdentifiers ids;
    JStarExpr* init;
} JStarVarDecl;

typedef struct JStarFunDecl {
    JStarIdentifier id;
    JStarFormalArgsList formalArgs;
    bool isGenerator;
    JStarStmt* body;
} JStarFunDecl;

typedef struct JStarNativeDecl {
    JStarIdentifier id;
    JStarFormalArgsList formalArgs;
} JStarNativeDecl;

typedef struct JStarClassDecl {
    JStarIdentifier id;
    JStarExpr* sup;
    JStarStmts methods;
} JStarClassDecl;

struct JStarDecl {
    bool isStatic;
    JStarExprs decorators;
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
    JStarStmts stmts;
} JStarBlockStmt;

typedef struct JStarImportStmt {
    JStarIdentifiers modules;
    JStarIdentifier as;
    JStarIdentifiers names;
} JStarImportStmt;

typedef struct JStarTryStmt {
    JStarStmt* block;
    JStarStmts excs;
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

typedef struct JStarReturnStmt {
    JStarExpr* e;
} JStarReturnStmt;

struct JStarStmt {
    JStarLoc loc;
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

JSTAR_API JStarExpr* jsrFunLiteral(JStarLoc loc, const JStarFormalArgsList* args, bool isGenerator,
                                   JStarStmt* body);
JSTAR_API JStarExpr* jsrTernaryExpr(JStarLoc loc, JStarExpr* cond, JStarExpr* thenExpr,
                                    JStarExpr* elseExpr);
JSTAR_API JStarExpr* jsrCompundAssignExpr(JStarLoc loc, JStarTokType op, JStarExpr* lval,
                                          JStarExpr* rval);
JSTAR_API JStarExpr* jsrPropertyAccessExpr(JStarLoc loc, JStarExpr* left, const char* name,
                                           size_t length);
JSTAR_API JStarExpr* jsrSuperLiteral(JStarLoc loc, JStarTok* name, JStarExpr* args);
JSTAR_API JStarExpr* jsrCallExpr(JStarLoc loc, JStarExpr* callee, JStarExpr* args);
JSTAR_API JStarExpr* jsrVarLiteral(JStarLoc loc, const char* str, size_t len);
JSTAR_API JStarExpr* jsrStrLiteral(JStarLoc loc, const char* str, size_t len);
JSTAR_API JStarExpr* jsrIndexExpr(JStarLoc loc, JStarExpr* left, JStarExpr* index);
JSTAR_API JStarExpr* jsrBinaryExpr(JStarLoc loc, JStarTokType op, JStarExpr* l, JStarExpr* r);
JSTAR_API JStarExpr* jsrUnaryExpr(JStarLoc loc, JStarTokType op, JStarExpr* operand);
JSTAR_API JStarExpr* jsrAssignExpr(JStarLoc loc, JStarExpr* lval, JStarExpr* rval);
JSTAR_API JStarExpr* jsrPowExpr(JStarLoc loc, JStarExpr* base, JStarExpr* exp);
JSTAR_API JStarExpr* jsrTableLiteral(JStarLoc loc, JStarExpr* keyVals);
JSTAR_API JStarExpr* jsrSpreadExpr(JStarLoc loc, JStarExpr* expr);
JSTAR_API JStarExpr* jsrExprList(JStarLoc loc, JStarExprs exprs);
JSTAR_API JStarExpr* jsrBoolLiteral(JStarLoc loc, bool boolean);
JSTAR_API JStarExpr* jsrTupleLiteral(JStarLoc loc, JStarExpr* exprs);
JSTAR_API JStarExpr* jsrListLiteral(JStarLoc loc, JStarExpr* exprs);
JSTAR_API JStarExpr* jsrYieldExpr(JStarLoc loc, JStarExpr* expr);
JSTAR_API JStarExpr* jsrNumLiteral(JStarLoc loc, double num);
JSTAR_API JStarExpr* jsrNullLiteral(JStarLoc loc);
JSTAR_API void jsrExprFree(JStarExpr* e);

// -----------------------------------------------------------------------------
// STATEMENT NODES
// -----------------------------------------------------------------------------

JSTAR_API JStarStmt* jsrFuncDecl(JStarLoc loc, const JStarIdentifier* name,
                                 const JStarFormalArgsList* args, bool isGenerator,
                                 JStarStmt* body);
JSTAR_API JStarStmt* jsrNativeDecl(JStarLoc loc, const JStarIdentifier* name,
                                   const JStarFormalArgsList* args);
JSTAR_API JStarStmt* jsrForStmt(JStarLoc loc, JStarStmt* init, JStarExpr* cond, JStarExpr* act,
                                JStarStmt* body);
JSTAR_API JStarStmt* jsrClassDecl(JStarLoc loc, const JStarIdentifier* clsName, JStarExpr* sup,
                                  JStarStmts methods);
JSTAR_API JStarStmt* jsrImportStmt(JStarLoc loc, JStarIdentifiers modules, JStarIdentifiers names,
                                   const JStarIdentifier* as);
JSTAR_API JStarStmt* jsrVarDecl(JStarLoc loc, bool isUnpack, JStarIdentifiers ids, JStarExpr* init);
JSTAR_API JStarStmt* jsrTryStmt(JStarLoc loc, JStarStmt* blck, JStarStmts excs, JStarStmt* ensure);
JSTAR_API JStarStmt* jsrIfStmt(JStarLoc loc, JStarExpr* cond, JStarStmt* thenStmt,
                               JStarStmt* elseStmt);
JSTAR_API JStarStmt* jsrForEachStmt(JStarLoc loc, JStarStmt* varDecl, JStarExpr* iter,
                                    JStarStmt* body);
JSTAR_API JStarStmt* jsrExceptStmt(JStarLoc loc, JStarExpr* cls, const JStarIdentifier* varName,
                                   JStarStmt* block);
JSTAR_API JStarStmt* jsrWithStmt(JStarLoc loc, JStarExpr* e, const JStarIdentifier* varName,
                                 JStarStmt* block);
JSTAR_API JStarStmt* jsrWhileStmt(JStarLoc loc, JStarExpr* cond, JStarStmt* body);
JSTAR_API JStarStmt* jsrBlockStmt(JStarLoc loc, JStarStmts list);
JSTAR_API JStarStmt* jsrReturnStmt(JStarLoc loc, JStarExpr* e);
JSTAR_API JStarStmt* jsrRaiseStmt(JStarLoc loc, JStarExpr* e);
JSTAR_API JStarStmt* jsrExprStmt(JStarLoc loc, JStarExpr* e);
JSTAR_API JStarStmt* jsrContinueStmt(JStarLoc loc);
JSTAR_API JStarStmt* jsrBreakStmt(JStarLoc loc);
JSTAR_API void jsrStmtFree(JStarStmt* s);

#endif
