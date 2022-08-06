#ifndef JSTAR_AST_H
#define JSTAR_AST_H

#include <stdbool.h>
#include <stdlib.h>

#include "../conf.h"
#include "lex.h"
#include "vector.h"

typedef struct JStarExpr JStarExpr;
typedef struct JStarStmt JStarStmt;

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
            bool unpackArg;
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
            bool unpackArg;
        } sup;
        double num;
        bool boolean;
        Vector list;
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

struct JStarStmt {
    int line;
    JStarStmtType type;
    // Declarations
    union {
        struct {
            bool isStatic;
            Vector decorators;
            union {
                struct {
                    bool isUnpack;
                    Vector ids;
                    JStarExpr* init;
                } var;
                struct {
                    JStarIdentifier id;
                    Vector formalArgs, defArgs;
                    bool isVararg;
                    bool isGenerator;
                    JStarStmt* body;
                } fun;
                struct {
                    JStarIdentifier id;
                    Vector formalArgs, defArgs;
                    bool isVararg;
                } native;
                struct {
                    JStarIdentifier id;
                    JStarExpr* sup;
                    Vector methods;
                } cls;
            } as;
        } decl;
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
            Vector stmts;
        } blockStmt;
        struct {
            Vector modules;
            JStarIdentifier as;
            Vector impNames;
        } importStmt;
        struct {
            JStarStmt* block;
            Vector excs;
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
    } as;
};

// -----------------------------------------------------------------------------
// IDENTIFIER FUNCTIONS
// -----------------------------------------------------------------------------

JSTAR_API JStarIdentifier* jsrNewIdentifier(size_t length, const char* name);
JSTAR_API bool jsrIdentifierEq(JStarIdentifier* id1, JStarIdentifier* id2);

// -----------------------------------------------------------------------------
// EXPRESSION NODES
// -----------------------------------------------------------------------------

JSTAR_API JStarExpr* jsrFuncLiteral(int line, Vector* args, Vector* defArgs, bool isVararg,
                                    bool isGenerator, JStarStmt* body);
JSTAR_API JStarExpr* jsrTernaryExpr(int line, JStarExpr* cond, JStarExpr* thenExpr,
                                    JStarExpr* elseExpr);
JSTAR_API JStarExpr* jsrCompundAssExpr(int line, JStarTokType op, JStarExpr* lval, JStarExpr* rval);
JSTAR_API JStarExpr* jsrAccessExpr(int line, JStarExpr* left, const char* name, size_t length);
JSTAR_API JStarExpr* jsrSuperLiteral(int line, JStarTok* name, JStarExpr* args, bool unpackArg);
JSTAR_API JStarExpr* jsrCallExpr(int line, JStarExpr* callee, JStarExpr* args, bool unpackArg);
JSTAR_API JStarExpr* jsrVarLiteral(int line, const char* str, size_t len);
JSTAR_API JStarExpr* jsrStrLiteral(int line, const char* str, size_t len);
JSTAR_API JStarExpr* jsrArrayAccExpr(int line, JStarExpr* left, JStarExpr* index);
JSTAR_API JStarExpr* jsrBinaryExpr(int line, JStarTokType op, JStarExpr* l, JStarExpr* r);
JSTAR_API JStarExpr* jsrUnaryExpr(int line, JStarTokType op, JStarExpr* operand);
JSTAR_API JStarExpr* jsrAssignExpr(int line, JStarExpr* lval, JStarExpr* rval);
JSTAR_API JStarExpr* jsrPowExpr(int line, JStarExpr* base, JStarExpr* exp);
JSTAR_API JStarExpr* jsrTableLiteral(int line, JStarExpr* keyVals);
JSTAR_API JStarExpr* jsrExprList(int line, Vector* exprs);
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

JSTAR_API JStarStmt* jsrFuncDecl(int line, JStarTok* name, Vector* args, Vector* defArgs,
                                 bool isVararg, bool isGenerator, JStarStmt* body);
JSTAR_API JStarStmt* jsrNativeDecl(int line, JStarTok* name, Vector* args, Vector* defArgs,
                                   bool vararg);
JSTAR_API JStarStmt* jsrForStmt(int line, JStarStmt* init, JStarExpr* cond, JStarExpr* act,
                                JStarStmt* body);
JSTAR_API JStarStmt* jsrIfStmt(int line, JStarExpr* cond, JStarStmt* thenStmt, JStarStmt* elseStmt);
JSTAR_API JStarStmt* jsrForEachStmt(int line, JStarStmt* varDecl, JStarExpr* iter, JStarStmt* body);
JSTAR_API JStarStmt* jsrExceptStmt(int line, JStarExpr* cls, JStarTok* varName, JStarStmt* block);
JSTAR_API JStarStmt* jsrClassDecl(int line, JStarTok* clsName, JStarExpr* sup, Vector* methods);
JSTAR_API JStarStmt* jsrImportStmt(int line, Vector* modules, Vector* impNames, JStarTok* as);
JSTAR_API JStarStmt* jsrWithStmt(int line, JStarExpr* e, JStarTok* varName, JStarStmt* block);
JSTAR_API JStarStmt* jsrVarDecl(int line, bool isUnpack, Vector* ids, JStarExpr* init);
JSTAR_API JStarStmt* jsrTryStmt(int line, JStarStmt* blck, Vector* excs, JStarStmt* ensure);
JSTAR_API JStarStmt* jsrWhileStmt(int line, JStarExpr* cond, JStarStmt* body);
JSTAR_API JStarStmt* jsrBlockStmt(int line, Vector* list);
JSTAR_API JStarStmt* jsrReturnStmt(int line, JStarExpr* e);
JSTAR_API JStarStmt* jsrRaiseStmt(int line, JStarExpr* e);
JSTAR_API JStarStmt* jsrExprStmt(int line, JStarExpr* e);
JSTAR_API JStarStmt* jsrContinueStmt(int line);
JSTAR_API JStarStmt* jsrBreakStmt(int line);
JSTAR_API void jsrStmtFree(JStarStmt* s);

#endif
