#ifndef AST_H
#define AST_H

#include "jstarconf.h"
#include "linkedlist.h"

#include <stdbool.h>
#include <stdlib.h>

typedef enum Operator {
    PLUS,
    MINUS,
    MULT,
    DIV,
    MOD,
    EQ,
    NEQ,
    AND,
    OR,
    NOT,
    GT,
    GE,
    LT,
    LE,
    IS,
    LENGTH,
    STRINGOP
} Operator;

typedef enum ExprType {
    BINARY,
    UNARY,
    ASSIGN,
    NUM_LIT,
    BOOL_LIT,
    STR_LIT,
    CMD_LIT,
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
    ANON_FUNC,
} ExprType;

typedef struct Identifier {
    size_t length;
    const char *name;
} Identifier;

JSTAR_API Identifier *newIdentifier(size_t length, const char *name);
JSTAR_API bool identifierEquals(Identifier *id1, Identifier *id2);

typedef struct Expr Expr;
typedef struct Stmt Stmt;

struct Expr {
    int line;
    ExprType type;
    union {
        struct {
            Operator op;
            Expr *left, *right;
        } binary;
        struct {
            Operator op;
            Expr *operand;
        } unary;
        struct {
            Expr *lval, *rval;
        } assign;
        struct {
            Operator op;
            Expr *lval, *rval;
        } compound;
        struct {
            size_t length;
            const char *str;
        } string;
        struct {
            Identifier id;
        } var;
        struct {
            LinkedList *lst;
        } list;
        struct {
            Expr *callee, *args;
        } call;
        struct {
            Expr *base, *exp;
        } exponent;
        struct {
            Expr *left;
            Identifier id;
        } access;
        struct {
            Expr *left;
            Expr *index;
        } arrayAccess;
        struct {
            Expr *exprs;
        } array;
        struct {
            Expr *exprs;
        } tuple;
        struct{
            Expr *keyVals;
        } table;
        struct {
            Expr *cond;
            Expr *thenExpr;
            Expr *elseExpr;
        } ternary;
        struct {
            Stmt *func;
        } anonFunc;
        struct {
            Identifier name;
            Expr *args;
        } sup;
        double num;
        bool boolean;
    } as;
};

JSTAR_API Expr *newBinary(int line, Operator op, Expr *l, Expr *r);
JSTAR_API Expr *newAssign(int line, Expr *lval, Expr *rval);
JSTAR_API Expr *newUnary(int line, Operator op, Expr *operand);
JSTAR_API Expr *newNullLiteral(int line);
JSTAR_API Expr *newNumLiteral(int line, double num);
JSTAR_API Expr *newBoolLiteral(int line, bool boolean);
JSTAR_API Expr *newArrayAccExpr(int line, Expr *left, Expr *index);
JSTAR_API Expr *newStrLiteral(int line, const char *str, size_t len);
JSTAR_API Expr *newCmdLiteral(int line, const char *cmd, size_t len);
JSTAR_API Expr *newVarLiteral(int line, const char *str, size_t len);
JSTAR_API Expr *newArrLiteral(int line, Expr *exprs);
JSTAR_API Expr *newTupleLiteral(int line, Expr *exprs);
JSTAR_API Expr *newTableLiteral(int line, Expr *keyVals);
JSTAR_API Expr *newExprList(int line, LinkedList *exprs);
JSTAR_API Expr *newCallExpr(int line, Expr *callee, LinkedList *args);
JSTAR_API Expr *newExpExpr(int line, Expr *base, Expr *exp);
JSTAR_API Expr *newAccessExpr(int line, Expr *left, const char *name, size_t length);
JSTAR_API Expr *newTernary(int line, Expr *cond, Expr *thenExpr, Expr *elseExpr);
JSTAR_API Expr *newCompoundAssing(int line, Operator op, Expr *lval, Expr *rval);
JSTAR_API Expr *newSuperLiteral(int line, const char *name, size_t len, Expr *args);
JSTAR_API Expr *newAnonymousFunc(int line, bool vararg, LinkedList *args, LinkedList *defArgs, Stmt *body);

JSTAR_API void freeExpr(Expr *e);

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
            Expr *cond;
            Stmt *thenStmt, *elseStmt;
        } ifStmt;
        struct {
            Stmt *init;
            Expr *cond, *act;
            Stmt *body;
        } forStmt;
        struct {
            Stmt *var;
            Expr *iterable;
            Stmt *body;
        } forEach;
        struct {
            Expr *cond;
            Stmt *body;
        } whileStmt;
        struct {
            Expr *e;
        } returnStmt;
        struct {
            LinkedList *stmts;
        } blockStmt;
        struct {
            bool isUnpack;
            LinkedList *ids;
            Expr *init;
        } varDecl;
        struct {
            Identifier id;
            LinkedList *formalArgs, *defArgs;
            bool isVararg;
            Stmt *body;
        } funcDecl;
        struct {
            Identifier id;
            LinkedList *formalArgs, *defArgs;
            bool isVararg;
        } nativeDecl;
        struct {
            Identifier id;
            Expr *sup;
            LinkedList *methods;
        } classDecl;
        struct {
            LinkedList *modules;
            Identifier as;
            LinkedList *impNames;
        } importStmt;
        struct {
            Stmt *block;
            LinkedList *excs;
            Stmt *ensure;
        } tryStmt;
        struct {
            Expr *cls;
            Identifier var;
            Stmt *block;
        } excStmt;
        struct {
            Expr *exc;
        } raiseStmt;
        struct {
            Expr *e;
            Identifier var;
            Stmt *block;
        } withStmt;
        Expr *exprStmt;
    } as;
};

JSTAR_API Stmt *newFuncDecl(int line, bool vararg, size_t length, const char *id, LinkedList *args, LinkedList *defArgs, Stmt *body);
JSTAR_API Stmt *newNativeDecl(int line, bool vararg, size_t length, const char *id, LinkedList *args, LinkedList *defArgs);
JSTAR_API Stmt *newImportStmt(int line, LinkedList *modules, LinkedList *impNames, const char *as, size_t asLength);
JSTAR_API Stmt *newClassDecl(int line, size_t clength, const char *cid, Expr *sup, LinkedList *methods);
JSTAR_API Stmt *newWithStmt(int line, Expr *e, size_t varLen, const char *varName, Stmt *block);
JSTAR_API Stmt *newExceptStmt(int line, Expr *cls, size_t vlen, const char *var, Stmt *block);
JSTAR_API Stmt *newForStmt(int line, Stmt *init, Expr *cond, Expr *act, Stmt *body);
JSTAR_API Stmt *newVarDecl(int line, bool isUnpack, LinkedList *ids, Expr *init);
JSTAR_API Stmt *newTryStmt(int line, Stmt *blck, LinkedList *excs, Stmt *ensure);
JSTAR_API Stmt *newIfStmt(int line, Expr *cond, Stmt *thenStmt, Stmt *elseStmt);
JSTAR_API Stmt *newForEach(int line, Stmt *varDecl, Expr *iter, Stmt *body);
JSTAR_API Stmt *newWhileStmt(int line, Expr *cond, Stmt *body);
JSTAR_API Stmt *newBlockStmt(int line, LinkedList *list);
JSTAR_API Stmt *newReturnStmt(int line, Expr *e);
JSTAR_API Stmt *newRaiseStmt(int line, Expr *e);
JSTAR_API Stmt *newExprStmt(int line, Expr *e);
JSTAR_API Stmt *newContinueStmt(int line);
JSTAR_API Stmt *newBreakStmt(int line);

JSTAR_API void freeStmt(Stmt *s);

#endif
