#ifndef AST_H
#define AST_H

#include <stdbool.h>
#include <stdlib.h>

#include "jstarconf.h"
#include "lex.h"
#include "vector.h"

typedef struct Identifier {
    size_t length;
    const char* name;
} Identifier;

JSTAR_API Identifier* newIdentifier(size_t length, const char* name);
JSTAR_API bool identifierEquals(Identifier* id1, Identifier* id2);

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

JSTAR_API Expr* newFunLit(int line, Vector* args, Vector* defArgs, bool vararg, Stmt* body);
JSTAR_API Expr* newAccessExpr(int line, Expr* left, const char* name, size_t length);
JSTAR_API Expr* newCompoundAssing(int line, TokenType op, Expr* lval, Expr* rval);
JSTAR_API Expr* newTernary(int line, Expr* cond, Expr* thenExpr, Expr* elseExpr);
JSTAR_API Expr* newVarLiteral(int line, const char* str, size_t len);
JSTAR_API Expr* newStrLiteral(int line, const char* str, size_t len);
JSTAR_API Expr* newArrayAccExpr(int line, Expr* left, Expr* index);
JSTAR_API Expr* newBinary(int line, TokenType op, Expr* l, Expr* r);
JSTAR_API Expr* newSuperLiteral(int line, Token* name, Expr* args);
JSTAR_API Expr* newCallExpr(int line, Expr* callee, Expr* args);
JSTAR_API Expr* newUnary(int line, TokenType op, Expr* operand);
JSTAR_API Expr* newAssign(int line, Expr* lval, Expr* rval);
JSTAR_API Expr* newExpExpr(int line, Expr* base, Expr* exp);
JSTAR_API Expr* newTableLiteral(int line, Expr* keyVals);
JSTAR_API Expr* newExprList(int line, Vector* exprs);
JSTAR_API Expr* newBoolLiteral(int line, bool boolean);
JSTAR_API Expr* newTupleLiteral(int line, Expr* exprs);
JSTAR_API Expr* newArrLiteral(int line, Expr* exprs);
JSTAR_API Expr* newNumLiteral(int line, double num);
JSTAR_API Expr* newNullLiteral(int line);

JSTAR_API void freeExpr(Expr* e);

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

JSTAR_API Stmt* newFuncDecl(int line, Token* name, Vector* args, Vector* defArgs, bool vararg,
                            Stmt* body);
JSTAR_API Stmt* newNativeDecl(int line, Token* name, Vector* args, Vector* defArgs, bool vararg);
JSTAR_API Stmt* newImportStmt(int line, Vector* modules, Vector* impNames, Token* as);
JSTAR_API Stmt* newClassDecl(int line, Token* clsName, Expr* sup, Vector* methods);
JSTAR_API Stmt* newWithStmt(int line, Expr* e, Token* varName, Stmt* block);
JSTAR_API Stmt* newExceptStmt(int line, Expr* cls, Token* varName, Stmt* block);
JSTAR_API Stmt* newForStmt(int line, Stmt* init, Expr* cond, Expr* act, Stmt* body);
JSTAR_API Stmt* newVarDecl(int line, bool isUnpack, Vector* ids, Expr* init);
JSTAR_API Stmt* newTryStmt(int line, Stmt* blck, Vector* excs, Stmt* ensure);
JSTAR_API Stmt* newIfStmt(int line, Expr* cond, Stmt* thenStmt, Stmt* elseStmt);
JSTAR_API Stmt* newForEach(int line, Stmt* varDecl, Expr* iter, Stmt* body);
JSTAR_API Stmt* newWhileStmt(int line, Expr* cond, Stmt* body);
JSTAR_API Stmt* newBlockStmt(int line, Vector* list);
JSTAR_API Stmt* newReturnStmt(int line, Expr* e);
JSTAR_API Stmt* newRaiseStmt(int line, Expr* e);
JSTAR_API Stmt* newExprStmt(int line, Expr* e);
JSTAR_API Stmt* newContinueStmt(int line);
JSTAR_API Stmt* newBreakStmt(int line);

JSTAR_API void freeStmt(Stmt* s);

#endif
