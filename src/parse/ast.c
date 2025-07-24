#include "parse/ast.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "conf.h"
#include "parse/lex.h"

// -----------------------------------------------------------------------------
// AST ARENA
// -----------------------------------------------------------------------------

#define ARENA_ALIGN(o, s) (-(uintptr_t)(o) & (s - 1))
#define ARENA_ALIGNMENT   (sizeof(void*))
#define ARENA_PAGE_SZ     (4096)

struct JStarASTArenaPage {
    JStarASTArenaPage* next;
    char *start, *end;
    char data[];
};

static JStarASTArenaPage* newPage(JStarASTArena* a, size_t requestedSize) {
    requestedSize += sizeof(JStarASTArenaPage);
    size_t pageSize = requestedSize <= ARENA_PAGE_SZ ? ARENA_PAGE_SZ : requestedSize;
    JStarASTArenaPage* page = malloc(pageSize);
    JSR_ASSERT(page, "Out of memory");
    page->next = NULL;
    page->start = page->data;
    page->end = page->data + pageSize - sizeof(JStarASTArenaPage);
    return page;
}

void* jsrASTArenaAlloc(JStarASTArena* a, size_t size) {
    size += ARENA_ALIGN(size, ARENA_ALIGNMENT);

    if(size > ARENA_PAGE_SZ - sizeof(JStarASTArenaPage)) {
        // Allocation is too large, add page to overflow list
        JStarASTArenaPage* page = newPage(a, size);
        JSR_ASSERT((size_t)(page->end - page->start) == size + sizeof(JStarASTArenaPage),
                   "Overflow page should be exactly `size + sizeof(JStarASTArenaPage)`");
        page->next = a->overflow;
        a->overflow = page;
        page->start = page->end;
        return page->data;
    }

    if(!a->last) {
        JSR_ASSERT(a->first == NULL && a->allocated == 0, "Should be first allocation");
        JStarASTArenaPage* page = newPage(a, size);
        a->first = page;
        a->last = page;
    }

    ptrdiff_t available = a->last->end - a->last->start;
    while(available < (ptrdiff_t)size) {
        JStarASTArenaPage* nextPage = a->last->next;
        if(!nextPage) {
            a->last->next = newPage(a, size);
            a->last = a->last->next;
            available = a->last->end - a->last->start;
            break;
        } else {
            a->last = nextPage;
            available = nextPage->end - nextPage->start;
        }
    }

    JSR_ASSERT(available >= (ptrdiff_t)size, "Not enough space in arena");

    void* p = a->last->start;
    JSR_ASSERT(ARENA_ALIGN(p, ARENA_ALIGNMENT) == 0,
               "Pointer is not aligned to the arena's alignment");

    a->last->start += size;
    a->allocated += size;
    return p;
}

void* jsrASTArenaRealloc(JStarASTArena* a, void* ptr, size_t oldSize, size_t newSize) {
    if(newSize <= oldSize) return ptr;
    void* newPtr = jsrASTArenaAlloc(a, newSize);
    memcpy(newPtr, ptr, oldSize);
    return newPtr;
}

static void freeOverflowPages(JStarASTArena* a) {
    JStarASTArenaPage* page = a->overflow;
    while(page) {
        JStarASTArenaPage* next = page->next;
        // TODO: do we actually want to just free overflow pages?
        free(page);
        page = next;
    }
    a->overflow = NULL;
}

void jsrASTArenaReset(JStarASTArena* a) {
    for(JStarASTArenaPage* p = a->first; p != NULL; p = p->next) {
        p->start = p->data;
    }
    freeOverflowPages(a);
    a->last = a->first;
    a->allocated = 0;
}

void jsrASTArenaFree(JStarASTArena* a) {
    JStarASTArenaPage* page = a->first;
    while(page) {
        JStarASTArenaPage* next = page->next;
        free(page);
        page = next;
    }
    freeOverflowPages(a);
    a->first = NULL;
    a->last = NULL;
    a->allocated = 0;
}

// -----------------------------------------------------------------------------
// IDENTIFIER
// -----------------------------------------------------------------------------

bool jsrIdentifierEq(JStarIdentifier id1, JStarIdentifier id2) {
    return id1.length == id2.length && (memcmp(id1.name, id2.name, id1.length) == 0);
}

// -----------------------------------------------------------------------------
// EXPRESSION NODES
// -----------------------------------------------------------------------------

static JStarExpr* newExpr(JStarASTArena* a, JStarLoc loc, JStarExprType type) {
    JStarExpr* e = jsrASTArenaAlloc(a, sizeof(*e));
    e->loc = loc;
    e->type = type;
    return e;
}

JStarExpr* jsrBinaryExpr(JStarASTArena* a, JStarLoc loc, JStarTokType op, JStarExpr* l,
                         JStarExpr* r) {
    JStarExpr* e = newExpr(a, loc, JSR_BINARY);
    e->as.binary.op = op;
    e->as.binary.left = l;
    e->as.binary.right = r;
    return e;
}

JStarExpr* jsrAssignExpr(JStarASTArena* a, JStarLoc loc, JStarExpr* lval, JStarExpr* rval) {
    JStarExpr* e = newExpr(a, loc, JSR_ASSIGN);
    e->as.assign.lval = lval;
    e->as.assign.rval = rval;
    return e;
}

JStarExpr* jsrUnaryExpr(JStarASTArena* a, JStarLoc loc, JStarTokType op, JStarExpr* operand) {
    JStarExpr* e = newExpr(a, loc, JSR_UNARY);
    e->as.unary.op = op;
    e->as.unary.operand = operand;
    return e;
}

JStarExpr* jsrNullLiteral(JStarASTArena* a, JStarLoc loc) {
    JStarExpr* e = newExpr(a, loc, JSR_NULL);
    return e;
}

JStarExpr* jsrNumLiteral(JStarASTArena* a, JStarLoc loc, double num) {
    JStarExpr* e = newExpr(a, loc, JSR_NUMBER);
    e->as.num = num;
    return e;
}

JStarExpr* jsrBoolLiteral(JStarASTArena* a, JStarLoc loc, bool boolean) {
    JStarExpr* e = newExpr(a, loc, JSR_BOOL);
    e->as.boolean = boolean;
    return e;
}

JStarExpr* jsrStrLiteral(JStarASTArena* a, JStarLoc loc, const char* str, size_t len) {
    JStarExpr* e = newExpr(a, loc, JSR_STRING);
    e->as.stringLiteral.str = str;
    e->as.stringLiteral.length = len;
    return e;
}

JStarExpr* jsrVarLiteral(JStarASTArena* a, JStarLoc loc, const char* var, size_t len) {
    JStarExpr* e = newExpr(a, loc, JSR_VAR);
    e->as.varLiteral.id.name = var;
    e->as.varLiteral.id.length = len;
    return e;
}

JStarExpr* jsrListLiteral(JStarASTArena* a, JStarLoc loc, JStarExpr* exprs) {
    JStarExpr* e = newExpr(a, loc, JSR_LIST);
    e->as.listLiteral.exprs = exprs;
    return e;
}

JStarExpr* jsrYieldExpr(JStarASTArena* a, JStarLoc loc, JStarExpr* expr) {
    JStarExpr* e = newExpr(a, loc, JSR_YIELD);
    e->as.yield.expr = expr;
    return e;
}

JStarExpr* jsrTupleLiteral(JStarASTArena* a, JStarLoc loc, JStarExpr* exprs) {
    JStarExpr* e = newExpr(a, loc, JSR_TUPLE);
    e->as.tupleLiteral.exprs = exprs;
    return e;
}

JStarExpr* jsrTableLiteral(JStarASTArena* a, JStarLoc loc, JStarExpr* keyVals) {
    JStarExpr* t = newExpr(a, loc, JSR_TABLE);
    t->as.tableLiteral.keyVals = keyVals;
    return t;
}

JStarExpr* jsrSpreadExpr(JStarASTArena* a, JStarLoc loc, JStarExpr* expr) {
    JStarExpr* s = newExpr(a, loc, JSR_SPREAD);
    s->as.spread.expr = expr;
    return s;
}

JStarExpr* jsrExprList(JStarASTArena* a, JStarLoc loc, JStarExprs exprs) {
    JStarExpr* e = newExpr(a, loc, JSR_EXPR_LST);
    e->as.exprList = exprs;
    return e;
}

JStarExpr* jsrCallExpr(JStarASTArena* a, JStarLoc loc, JStarExpr* callee, JStarExpr* args) {
    JStarExpr* e = newExpr(a, loc, JSR_CALL);
    e->as.call.callee = callee;
    e->as.call.args = args;
    return e;
}

JStarExpr* jsrPowExpr(JStarASTArena* a, JStarLoc loc, JStarExpr* base, JStarExpr* exp) {
    JStarExpr* e = newExpr(a, loc, JSR_POWER);
    e->as.pow.base = base;
    e->as.pow.exp = exp;
    return e;
}

JStarExpr* jsrPropertyAccessExpr(JStarASTArena* a, JStarLoc loc, JStarExpr* left, const char* name,
                                 size_t length) {
    JStarExpr* e = newExpr(a, loc, JSR_PROPERTY_ACCESS);
    e->as.propertyAccess.left = left;
    e->as.propertyAccess.id.name = name;
    e->as.propertyAccess.id.length = length;
    return e;
}

JStarExpr* jsrIndexExpr(JStarASTArena* a, JStarLoc loc, JStarExpr* left, JStarExpr* index) {
    JStarExpr* e = newExpr(a, loc, JSR_INDEX);
    e->as.index.left = left;
    e->as.index.index = index;
    return e;
}

JStarExpr* jsrTernaryExpr(JStarASTArena* a, JStarLoc loc, JStarExpr* cond, JStarExpr* thenExpr,
                          JStarExpr* elseExpr) {
    JStarExpr* e = newExpr(a, loc, JSR_TERNARY);
    e->as.ternary.cond = cond;
    e->as.ternary.thenExpr = thenExpr;
    e->as.ternary.elseExpr = elseExpr;
    return e;
}

JStarExpr* jsrCompundAssignExpr(JStarASTArena* a, JStarLoc loc, JStarTokType op, JStarExpr* lval,
                                JStarExpr* rval) {
    JStarExpr* e = newExpr(a, loc, JSR_COMPOUND_ASSIGN);
    e->as.compoundAssign.op = op;
    e->as.compoundAssign.lval = lval;
    e->as.compoundAssign.rval = rval;
    return e;
}

JStarExpr* jsrFunLiteral(JStarASTArena* a, JStarLoc loc, JStarFormalArgsList args, bool isGenerator,
                         JStarStmt* body) {
    JStarExpr* e = newExpr(a, loc, JSR_FUN_LIT);
    e->as.funLit.func = jsrFuncDecl(a, loc, (JStarIdentifier){0}, args, isGenerator, body);
    return e;
}

JStarExpr* jsrSuperLiteral(JStarASTArena* a, JStarLoc loc, JStarTok* name, JStarExpr* args) {
    JStarExpr* e = newExpr(a, loc, JSR_SUPER);
    e->as.sup.name = (JStarIdentifier){name->length, name->lexeme};
    e->as.sup.args = args;
    return e;
}

// -----------------------------------------------------------------------------
// STATEMENT NODES
// -----------------------------------------------------------------------------

static JStarStmt* newStmt(JStarASTArena* a, JStarLoc loc, JStarStmtType type) {
    JStarStmt* s = jsrASTArenaAlloc(a, sizeof(*s));
    s->loc = loc;
    s->type = type;
    return s;
}

static JStarStmt* newDecl(JStarASTArena* a, JStarLoc loc, JStarStmtType type) {
    JSR_ASSERT((type == JSR_VARDECL || type == JSR_FUNCDECL || type == JSR_CLASSDECL ||
                type == JSR_NATIVEDECL),
               "Not a declaration");
    JStarStmt* s = newStmt(a, loc, type);
    s->as.decl.isStatic = false;
    s->as.decl.decorators = (JStarExprs){0};
    return s;
}

JStarStmt* jsrFuncDecl(JStarASTArena* a, JStarLoc loc, JStarIdentifier name,
                       JStarFormalArgsList args, bool isGenerator, JStarStmt* body) {
    JStarStmt* f = newDecl(a, loc, JSR_FUNCDECL);
    f->as.decl.as.fun.id = name;
    f->as.decl.as.fun.formalArgs = args;
    f->as.decl.as.fun.body = body;
    f->as.decl.as.fun.isGenerator = isGenerator;
    return f;
}

JStarStmt* jsrNativeDecl(JStarASTArena* a, JStarLoc loc, JStarIdentifier name,
                         JStarFormalArgsList args) {
    JStarStmt* n = newDecl(a, loc, JSR_NATIVEDECL);
    n->as.decl.as.native.id = name;
    n->as.decl.as.native.formalArgs = args;
    return n;
}

JStarStmt* jsrClassDecl(JStarASTArena* a, JStarLoc loc, JStarIdentifier clsName, JStarExpr* sup,
                        JStarStmts methods) {
    JStarStmt* c = newDecl(a, loc, JSR_CLASSDECL);
    c->as.decl.as.cls.sup = sup;
    c->as.decl.as.cls.id = clsName;
    c->as.decl.as.cls.methods = methods;
    return c;
}

JStarStmt* jsrVarDecl(JStarASTArena* a, JStarLoc loc, bool isUnpack, JStarIdentifiers ids,
                      JStarExpr* init) {
    JStarStmt* s = newDecl(a, loc, JSR_VARDECL);
    s->as.decl.as.var.ids = ids;
    s->as.decl.as.var.isUnpack = isUnpack;
    s->as.decl.as.var.init = init;
    return s;
}

JStarStmt* jsrWithStmt(JStarASTArena* a, JStarLoc loc, JStarExpr* e, JStarIdentifier varName,
                       JStarStmt* block) {
    JStarStmt* w = newStmt(a, loc, JSR_WITH);
    w->as.withStmt.e = e;
    w->as.withStmt.var = varName;
    w->as.withStmt.block = block;
    return w;
}

JStarStmt* jsrForStmt(JStarASTArena* a, JStarLoc loc, JStarStmt* init, JStarExpr* cond,
                      JStarExpr* act, JStarStmt* body) {
    JStarStmt* s = newStmt(a, loc, JSR_FOR);
    s->as.forStmt.init = init;
    s->as.forStmt.cond = cond;
    s->as.forStmt.act = act;
    s->as.forStmt.body = body;
    return s;
}

JStarStmt* jsrForEachStmt(JStarASTArena* a, JStarLoc loc, JStarStmt* var, JStarExpr* iter,
                          JStarStmt* body) {
    JStarStmt* s = newStmt(a, loc, JSR_FOREACH);
    s->as.forEach.var = var;
    s->as.forEach.iterable = iter;
    s->as.forEach.body = body;
    return s;
}

JStarStmt* jsrWhileStmt(JStarASTArena* a, JStarLoc loc, JStarExpr* cond, JStarStmt* body) {
    JStarStmt* s = newStmt(a, loc, JSR_WHILE);
    s->as.whileStmt.cond = cond;
    s->as.whileStmt.body = body;
    return s;
}

JStarStmt* jsrReturnStmt(JStarASTArena* a, JStarLoc loc, JStarExpr* e) {
    JStarStmt* s = newStmt(a, loc, JSR_RETURN);
    s->as.returnStmt.e = e;
    return s;
}

JStarStmt* jsrIfStmt(JStarASTArena* a, JStarLoc loc, JStarExpr* cond, JStarStmt* thenStmt,
                     JStarStmt* elseStmt) {
    JStarStmt* s = newStmt(a, loc, JSR_IF);
    s->as.ifStmt.cond = cond;
    s->as.ifStmt.thenStmt = thenStmt;
    s->as.ifStmt.elseStmt = elseStmt;
    return s;
}

JStarStmt* jsrBlockStmt(JStarASTArena* a, JStarLoc loc, JStarStmts list) {
    JStarStmt* s = newStmt(a, loc, JSR_BLOCK);
    s->as.blockStmt.stmts = list;
    return s;
}

JStarStmt* jsrImportStmt(JStarASTArena* a, JStarLoc loc, JStarIdentifiers modules,
                         JStarIdentifiers names, JStarIdentifier as) {
    JStarStmt* s = newStmt(a, loc, JSR_IMPORT);
    s->as.importStmt.modules = modules;
    s->as.importStmt.names = names;
    s->as.importStmt.as = as;
    return s;
}

JStarStmt* jsrExprStmt(JStarASTArena* a, JStarLoc loc, JStarExpr* e) {
    JStarStmt* s = newStmt(a, loc, JSR_EXPR_STMT);
    s->as.exprStmt = e;
    return s;
}

JStarStmt* jsrTryStmt(JStarASTArena* a, JStarLoc loc, JStarStmt* blck, JStarStmts excs,
                      JStarStmt* ensure) {
    JStarStmt* s = newStmt(a, loc, JSR_TRY);
    s->as.tryStmt.block = blck;
    s->as.tryStmt.excs = excs;
    s->as.tryStmt.ensure = ensure;
    return s;
}

JStarStmt* jsrExceptStmt(JStarASTArena* a, JStarLoc loc, JStarExpr* cls, JStarIdentifier varName,
                         JStarStmt* block) {
    JStarStmt* s = newStmt(a, loc, JSR_EXCEPT);
    s->as.excStmt.block = block;
    s->as.excStmt.cls = cls;
    s->as.excStmt.var = varName;
    return s;
}

JStarStmt* jsrRaiseStmt(JStarASTArena* a, JStarLoc loc, JStarExpr* e) {
    JStarStmt* s = newStmt(a, loc, JSR_RAISE);
    s->as.raiseStmt.exc = e;
    return s;
}

JStarStmt* jsrContinueStmt(JStarASTArena* a, JStarLoc loc) {
    JStarStmt* s = newStmt(a, loc, JSR_CONTINUE);
    s->as.exprStmt = NULL;
    return s;
}

JStarStmt* jsrBreakStmt(JStarASTArena* a, JStarLoc loc) {
    JStarStmt* s = newStmt(a, loc, JSR_BREAK);
    s->as.exprStmt = NULL;
    return s;
}
