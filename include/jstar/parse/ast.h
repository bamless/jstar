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

typedef struct JStarIdentifier {
    int length;
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

typedef struct JStarFormalArgsList {
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
// AST ARENA
// -----------------------------------------------------------------------------

#define JSR_AST_ARRAY_INIT_CAP 8

#define jsrASTArrayForeach(T, it, arr) \
    for(T* it = (arr)->items; it < (arr)->items + (arr)->count; ++it)

#define jsrASTArrayReserve(arena, arr, newCapacity)                                                \
    do {                                                                                           \
        if((newCapacity) > (arr)->capacity) {                                                      \
            size_t oldCap = (arr)->capacity;                                                       \
            if((arr)->capacity == 0) {                                                             \
                (arr)->capacity = JSR_AST_ARRAY_INIT_CAP;                                          \
            }                                                                                      \
            while((newCapacity) > (arr)->capacity) {                                               \
                (arr)->capacity *= 2;                                                              \
            }                                                                                      \
            (arr)->items = jsrASTArenaRealloc(arena, (arr)->items, oldCap * sizeof(*(arr)->items), \
                                              (arr)->capacity * sizeof(*(arr)->items));            \
        }                                                                                          \
    } while(0)

#define jsrASTArrayAppend(arena, arr, item)               \
    do {                                                  \
        jsrASTArrayReserve(arena, arr, (arr)->count + 1); \
        (arr)->items[(arr)->count++] = (item);            \
    } while(0)

typedef struct JStarASTArenaPage JStarASTArenaPage;
typedef struct {
    JStarASTArenaPage *first, *last;
    JStarASTArenaPage* overflow;
    size_t allocated;
} JStarASTArena;

JSTAR_API void* jsrASTArenaAlloc(JStarASTArena* a, size_t size);
JSTAR_API void* jsrASTArenaRealloc(JStarASTArena* a, void* ptr, size_t oldSize, size_t newSize);
JSTAR_API void jsrASTArenaReset(JStarASTArena* a);
JSTAR_API void jsrASTArenaFree(JStarASTArena* a);

// -----------------------------------------------------------------------------
// IDENTIFIER FUNCTIONS
// -----------------------------------------------------------------------------

JSTAR_API bool jsrIdentifierEq(JStarIdentifier id1, JStarIdentifier id2);

// -----------------------------------------------------------------------------
// EXPRESSION NODES
// -----------------------------------------------------------------------------

JSTAR_API JStarExpr* jsrFunLiteral(JStarASTArena* a, JStarLoc loc, JStarFormalArgsList args,
                                   bool isGenerator, JStarStmt* body);
JSTAR_API JStarExpr* jsrTernaryExpr(JStarASTArena* a, JStarLoc loc, JStarExpr* cond,
                                    JStarExpr* thenExpr, JStarExpr* elseExpr);
JSTAR_API JStarExpr* jsrCompundAssignExpr(JStarASTArena* a, JStarLoc loc, JStarTokType op,
                                          JStarExpr* lval, JStarExpr* rval);
JSTAR_API JStarExpr* jsrPropertyAccessExpr(JStarASTArena* a, JStarLoc loc, JStarExpr* left,
                                           const char* name, size_t length);
JSTAR_API JStarExpr* jsrSuperLiteral(JStarASTArena* a, JStarLoc loc, JStarTok* name,
                                     JStarExpr* args);
JSTAR_API JStarExpr* jsrCallExpr(JStarASTArena* a, JStarLoc loc, JStarExpr* callee,
                                 JStarExpr* args);
JSTAR_API JStarExpr* jsrVarLiteral(JStarASTArena* a, JStarLoc loc, const char* str, size_t len);
JSTAR_API JStarExpr* jsrStrLiteral(JStarASTArena* a, JStarLoc loc, const char* str, size_t len);
JSTAR_API JStarExpr* jsrIndexExpr(JStarASTArena* a, JStarLoc loc, JStarExpr* left,
                                  JStarExpr* index);
JSTAR_API JStarExpr* jsrBinaryExpr(JStarASTArena* a, JStarLoc loc, JStarTokType op, JStarExpr* l,
                                   JStarExpr* r);
JSTAR_API JStarExpr* jsrUnaryExpr(JStarASTArena* a, JStarLoc loc, JStarTokType op,
                                  JStarExpr* operand);
JSTAR_API JStarExpr* jsrAssignExpr(JStarASTArena* a, JStarLoc loc, JStarExpr* lval,
                                   JStarExpr* rval);
JSTAR_API JStarExpr* jsrPowExpr(JStarASTArena* a, JStarLoc loc, JStarExpr* base, JStarExpr* exp);
JSTAR_API JStarExpr* jsrTableLiteral(JStarASTArena* a, JStarLoc loc, JStarExpr* keyVals);
JSTAR_API JStarExpr* jsrSpreadExpr(JStarASTArena* a, JStarLoc loc, JStarExpr* expr);
JSTAR_API JStarExpr* jsrExprList(JStarASTArena* a, JStarLoc loc, JStarExprs exprs);
JSTAR_API JStarExpr* jsrBoolLiteral(JStarASTArena* a, JStarLoc loc, bool boolean);
JSTAR_API JStarExpr* jsrTupleLiteral(JStarASTArena* a, JStarLoc loc, JStarExpr* exprs);
JSTAR_API JStarExpr* jsrListLiteral(JStarASTArena* a, JStarLoc loc, JStarExpr* exprs);
JSTAR_API JStarExpr* jsrYieldExpr(JStarASTArena* a, JStarLoc loc, JStarExpr* expr);
JSTAR_API JStarExpr* jsrNumLiteral(JStarASTArena* a, JStarLoc loc, double num);
JSTAR_API JStarExpr* jsrNullLiteral(JStarASTArena* a, JStarLoc loc);

// -----------------------------------------------------------------------------
// STATEMENT NODES
// -----------------------------------------------------------------------------

JSTAR_API JStarStmt* jsrFuncDecl(JStarASTArena* a, JStarLoc loc, JStarIdentifier name,
                                 JStarFormalArgsList args, bool isGenerator, JStarStmt* body);
JSTAR_API JStarStmt* jsrNativeDecl(JStarASTArena* a, JStarLoc loc, JStarIdentifier name,
                                   JStarFormalArgsList args);
JSTAR_API JStarStmt* jsrForStmt(JStarASTArena* a, JStarLoc loc, JStarStmt* init, JStarExpr* cond,
                                JStarExpr* act, JStarStmt* body);
JSTAR_API JStarStmt* jsrClassDecl(JStarASTArena* a, JStarLoc loc, JStarIdentifier clsName,
                                  JStarExpr* sup, JStarStmts methods);
JSTAR_API JStarStmt* jsrImportStmt(JStarASTArena* a, JStarLoc loc, JStarIdentifiers modules,
                                   JStarIdentifiers names, JStarIdentifier as);
JSTAR_API JStarStmt* jsrVarDecl(JStarASTArena* a, JStarLoc loc, bool isUnpack, JStarIdentifiers ids,
                                JStarExpr* init);
JSTAR_API JStarStmt* jsrTryStmt(JStarASTArena* a, JStarLoc loc, JStarStmt* blck, JStarStmts excs,
                                JStarStmt* ensure);
JSTAR_API JStarStmt* jsrIfStmt(JStarASTArena* a, JStarLoc loc, JStarExpr* cond, JStarStmt* thenStmt,
                               JStarStmt* elseStmt);
JSTAR_API JStarStmt* jsrForEachStmt(JStarASTArena* a, JStarLoc loc, JStarStmt* varDecl,
                                    JStarExpr* iter, JStarStmt* body);
JSTAR_API JStarStmt* jsrExceptStmt(JStarASTArena* a, JStarLoc loc, JStarExpr* cls,
                                   JStarIdentifier varName, JStarStmt* block);
JSTAR_API JStarStmt* jsrWithStmt(JStarASTArena* a, JStarLoc loc, JStarExpr* e,
                                 JStarIdentifier varName, JStarStmt* block);
JSTAR_API JStarStmt* jsrWhileStmt(JStarASTArena* a, JStarLoc loc, JStarExpr* cond, JStarStmt* body);
JSTAR_API JStarStmt* jsrBlockStmt(JStarASTArena* a, JStarLoc loc, JStarStmts list);
JSTAR_API JStarStmt* jsrReturnStmt(JStarASTArena* a, JStarLoc loc, JStarExpr* e);
JSTAR_API JStarStmt* jsrRaiseStmt(JStarASTArena* a, JStarLoc loc, JStarExpr* e);
JSTAR_API JStarStmt* jsrExprStmt(JStarASTArena* a, JStarLoc loc, JStarExpr* e);
JSTAR_API JStarStmt* jsrContinueStmt(JStarASTArena* a, JStarLoc loc);
JSTAR_API JStarStmt* jsrBreakStmt(JStarASTArena* a, JStarLoc loc);

#endif
