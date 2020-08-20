#ifndef AST_H
#define AST_H

#include <stdbool.h>
#include <stdlib.h>

#include "../jstarconf.h"
#include "lex.h"
#include "vector.h"

typedef struct Identifier {
    size_t length;
    const char* name;
} Identifier;

JSTAR_API Identifier* jsrIdentifier(size_t length, const char* name);
JSTAR_API bool jsrIdentifierEq(Identifier* id1, Identifier* id2);

typedef enum ExprType {
    BINARY,
    UNARY,
    ASSIGN,
    NUM_LIT,
    BOOL_LIT,
    STR_LIT,
    VAR_LIT,
    NULL_LIT,
    EXPR_LST,
    CALL_EXPR,
    EXP_EXPR,
    SUPER_LIT,
    ACCESS_EXPR,
    ARR_LIT,
    TUPLE_LIT,
    TABLE_LIT,
    ARR_ACC,
    TERNARY,
    COMP_ASSIGN,
    FUN_LIT,
} ExprType;

typedef struct Expr Expr;
typedef struct Stmt Stmt;

struct Expr {
    int line;
    ExprType type;
    union {
        struct {
            TokenType op;
            Expr *left, *right;
        } binary;
        struct {
            TokenType op;
            Expr* operand;
        } unary;
        struct {
            Expr *lval, *rval;
        } assign;
        struct {
            TokenType op;
            Expr *lval, *rval;
        } compound;
        struct {
            size_t length;
            const char* str;
        } string;
        struct {
            Identifier id;
        } var;
        struct {
            Expr *callee, *args;
        } call;
        struct {
            Expr *base, *exp;
        } exponent;
        struct {
            Expr* left;
            Identifier id;
        } access;
        struct {
            Expr* left;
            Expr* index;
        } arrayAccess;
        struct {
            Expr* exprs;
        } array;
        struct {
            Expr* exprs;
        } tuple;
        struct {
            Expr* keyVals;
        } table;
        struct {
            Expr* cond;
            Expr* thenExpr;
            Expr* elseExpr;
        } ternary;
        struct {
            Stmt* func;
        } funLit;
        struct {
            Identifier name;
            Expr* args;
        } sup;
        double num;
        bool boolean;
        Vector list;
    } as;
};

JSTAR_API Expr* jsrFunLit(int line, Vector* args, Vector* defArgs, bool vararg, Stmt* body);
JSTAR_API Expr* jsrAccessExpr(int line, Expr* left, const char* name, size_t length);
JSTAR_API Expr* jsrCompoundAssing(int line, TokenType op, Expr* lval, Expr* rval);
JSTAR_API Expr* jsrTernary(int line, Expr* cond, Expr* thenExpr, Expr* elseExpr);
JSTAR_API Expr* jsrVarLiteral(int line, const char* str, size_t len);
JSTAR_API Expr* jsrStrLiteral(int line, const char* str, size_t len);
JSTAR_API Expr* jsrArrayAccExpr(int line, Expr* left, Expr* index);
JSTAR_API Expr* jsrBinary(int line, TokenType op, Expr* l, Expr* r);
JSTAR_API Expr* jsrSuperLiteral(int line, Token* name, Expr* args);
JSTAR_API Expr* jsrCallExpr(int line, Expr* callee, Expr* args);
JSTAR_API Expr* jsrUnary(int line, TokenType op, Expr* operand);
JSTAR_API Expr* jsrAssign(int line, Expr* lval, Expr* rval);
JSTAR_API Expr* jsrExpExpr(int line, Expr* base, Expr* exp);
JSTAR_API Expr* jsrTableLiteral(int line, Expr* keyVals);
JSTAR_API Expr* jsrExprList(int line, Vector* exprs);
JSTAR_API Expr* jsrBoolLiteral(int line, bool boolean);
JSTAR_API Expr* jsrTupleLiteral(int line, Expr* exprs);
JSTAR_API Expr* jsrArrLiteral(int line, Expr* exprs);
JSTAR_API Expr* jsrNumLiteral(int line, double num);
JSTAR_API Expr* jsrNullLiteral(int line);
JSTAR_API void jsrExprFree(Expr* e);

typedef enum StmtType {
    IF,
    FOR,
    WHILE,
    FOREACH,
    BLOCK,
    RETURN_STMT,
    EXPR,
    VARDECL,
    FUNCDECL,
    NATIVEDECL,
    CLASSDECL,
    IMPORT,
    TRY_STMT,
    EXCEPT_STMT,
    RAISE_STMT,
    WITH_STMT,
    CONTINUE_STMT,
    BREAK_STMT
} StmtType;

struct Stmt {
    int line;
    StmtType type;
    union {
        struct {
            Expr* cond;
            Stmt *thenStmt, *elseStmt;
        } ifStmt;
        struct {
            Stmt* init;
            Expr *cond, *act;
            Stmt* body;
        } forStmt;
        struct {
            Stmt* var;
            Expr* iterable;
            Stmt* body;
        } forEach;
        struct {
            Expr* cond;
            Stmt* body;
        } whileStmt;
        struct {
            Expr* e;
        } returnStmt;
        struct {
            Vector stmts;
        } blockStmt;
        struct {
            bool isUnpack;
            Vector ids;
            Expr* init;
        } varDecl;
        struct {
            Identifier id;
            Vector formalArgs, defArgs;
            bool isVararg;
            Stmt* body;
        } funcDecl;
        struct {
            Identifier id;
            Vector formalArgs, defArgs;
            bool isVararg;
        } nativeDecl;
        struct {
            Identifier id;
            Expr* sup;
            Vector methods;
        } classDecl;
        struct {
            Vector modules;
            Identifier as;
            Vector impNames;
        } importStmt;
        struct {
            Stmt* block;
            Vector excs;
            Stmt* ensure;
        } tryStmt;
        struct {
            Expr* cls;
            Identifier var;
            Stmt* block;
        } excStmt;
        struct {
            Expr* exc;
        } raiseStmt;
        struct {
            Expr* e;
            Identifier var;
            Stmt* block;
        } withStmt;
        Expr* exprStmt;
    } as;
};

JSTAR_API Stmt* jsrFuncDecl(int line, Token* name, Vector* args, Vector* defArgs, bool vararg,
                            Stmt* body);
JSTAR_API Stmt* jsrNativeDecl(int line, Token* name, Vector* args, Vector* defArgs, bool vararg);
JSTAR_API Stmt* jsrImportStmt(int line, Vector* modules, Vector* impNames, Token* as);
JSTAR_API Stmt* jsrClassDecl(int line, Token* clsName, Expr* sup, Vector* methods);
JSTAR_API Stmt* jsrWithStmt(int line, Expr* e, Token* varName, Stmt* block);
JSTAR_API Stmt* jsrExceptStmt(int line, Expr* cls, Token* varName, Stmt* block);
JSTAR_API Stmt* jsrForStmt(int line, Stmt* init, Expr* cond, Expr* act, Stmt* body);
JSTAR_API Stmt* jsrVarDecl(int line, bool isUnpack, Vector* ids, Expr* init);
JSTAR_API Stmt* jsrTryStmt(int line, Stmt* blck, Vector* excs, Stmt* ensure);
JSTAR_API Stmt* jsrIfStmt(int line, Expr* cond, Stmt* thenStmt, Stmt* elseStmt);
JSTAR_API Stmt* jsrForEach(int line, Stmt* varDecl, Expr* iter, Stmt* body);
JSTAR_API Stmt* jsrWhileStmt(int line, Expr* cond, Stmt* body);
JSTAR_API Stmt* jsrBlockStmt(int line, Vector* list);
JSTAR_API Stmt* jsrReturnStmt(int line, Expr* e);
JSTAR_API Stmt* jsrRaiseStmt(int line, Expr* e);
JSTAR_API Stmt* jsrExprStmt(int line, Expr* e);
JSTAR_API Stmt* jsrContinueStmt(int line);
JSTAR_API Stmt* jsrBreakStmt(int line);
JSTAR_API void jsrStmtFree(Stmt* s);

#endif
