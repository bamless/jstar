#include "parse/ast.h"

#include <string.h>

#include "conf.h"
#include "parse/lex.h"
#include "parse/vector.h"

bool jsrIdentifierEq(const JStarIdentifier* id1, const JStarIdentifier* id2) {
    return id1->length == id2->length && (memcmp(id1->name, id2->name, id1->length) == 0);
}

// -----------------------------------------------------------------------------
// EXPRESSION NODES
// -----------------------------------------------------------------------------

static JStarExpr* newExpr(int line, JStarExprType type) {
    JStarExpr* e = malloc(sizeof(*e));
    JSR_ASSERT(e, "Out of memory");
    e->line = line;
    e->type = type;
    return e;
}

JStarExpr* jsrBinaryExpr(int line, JStarTokType op, JStarExpr* l, JStarExpr* r) {
    JStarExpr* e = newExpr(line, JSR_BINARY);
    e->as.binary.op = op;
    e->as.binary.left = l;
    e->as.binary.right = r;
    return e;
}

JStarExpr* jsrAssignExpr(int line, JStarExpr* lval, JStarExpr* rval) {
    JStarExpr* e = newExpr(line, JSR_ASSIGN);
    e->as.assign.lval = lval;
    e->as.assign.rval = rval;
    return e;
}

JStarExpr* jsrUnaryExpr(int line, JStarTokType op, JStarExpr* operand) {
    JStarExpr* e = newExpr(line, JSR_UNARY);
    e->as.unary.op = op;
    e->as.unary.operand = operand;
    return e;
}

JStarExpr* jsrNullLiteral(int line) {
    JStarExpr* e = newExpr(line, JSR_NULL);
    return e;
}

JStarExpr* jsrNumLiteral(int line, double num) {
    JStarExpr* e = newExpr(line, JSR_NUMBER);
    e->as.num = num;
    return e;
}

JStarExpr* jsrBoolLiteral(int line, bool boolean) {
    JStarExpr* e = newExpr(line, JSR_BOOL);
    e->as.boolean = boolean;
    return e;
}

JStarExpr* jsrStrLiteral(int line, const char* str, size_t len) {
    JStarExpr* e = newExpr(line, JSR_STRING);
    e->as.stringLiteral.str = str;
    e->as.stringLiteral.length = len;
    return e;
}

JStarExpr* jsrVarLiteral(int line, const char* var, size_t len) {
    JStarExpr* e = newExpr(line, JSR_VAR);
    e->as.varLiteral.id.name = var;
    e->as.varLiteral.id.length = len;
    return e;
}

JStarExpr* jsrListLiteral(int line, JStarExpr* exprs) {
    JStarExpr* a = newExpr(line, JSR_LIST);
    a->as.listLiteral.exprs = exprs;
    return a;
}

JStarExpr* jsrYieldExpr(int line, JStarExpr* expr) {
    JStarExpr* e = newExpr(line, JSR_YIELD);
    e->as.yield.expr = expr;
    return e;
}

JStarExpr* jsrTupleLiteral(int line, JStarExpr* exprs) {
    JStarExpr* a = newExpr(line, JSR_TUPLE);
    a->as.tupleLiteral.exprs = exprs;
    return a;
}

JStarExpr* jsrTableLiteral(int line, JStarExpr* keyVals) {
    JStarExpr* t = newExpr(line, JSR_TABLE);
    t->as.tableLiteral.keyVals = keyVals;
    return t;
}

JStarExpr* jsrSpreadExpr(int line, JStarExpr* expr) {
    JStarExpr* s = newExpr(line, JSR_SPREAD);
    s->as.spread.expr = expr;
    return s;
}

JStarExpr* jsrExprList(int line, ext_vector(JStarExpr*) exprs) {
    JStarExpr* e = newExpr(line, JSR_EXPR_LST);
    e->as.exprList = exprs;
    return e;
}

JStarExpr* jsrCallExpr(int line, JStarExpr* callee, JStarExpr* args) {
    JStarExpr* e = newExpr(line, JSR_CALL);
    e->as.call.callee = callee;
    e->as.call.args = args;
    return e;
}

JStarExpr* jsrPowExpr(int line, JStarExpr* base, JStarExpr* exp) {
    JStarExpr* e = newExpr(line, JSR_POWER);
    e->as.pow.base = base;
    e->as.pow.exp = exp;
    return e;
}

JStarExpr* jsrPropertyAccessExpr(int line, JStarExpr* left, const char* name, size_t length) {
    JStarExpr* e = newExpr(line, JSR_PROPERTY_ACCESS);
    e->as.propertyAccess.left = left;
    e->as.propertyAccess.id.name = name;
    e->as.propertyAccess.id.length = length;
    return e;
}

JStarExpr* jsrIndexExpr(int line, JStarExpr* left, JStarExpr* index) {
    JStarExpr* e = newExpr(line, JSR_INDEX);
    e->as.index.left = left;
    e->as.index.index = index;
    return e;
}

JStarExpr* jsrTernaryExpr(int line, JStarExpr* cond, JStarExpr* thenExpr, JStarExpr* elseExpr) {
    JStarExpr* e = newExpr(line, JSR_TERNARY);
    e->as.ternary.cond = cond;
    e->as.ternary.thenExpr = thenExpr;
    e->as.ternary.elseExpr = elseExpr;
    return e;
}

JStarExpr* jsrCompundAssignExpr(int line, JStarTokType op, JStarExpr* lval, JStarExpr* rval) {
    JStarExpr* e = newExpr(line, JSR_COMPOUND_ASSIGN);
    e->as.compoundAssign.op = op;
    e->as.compoundAssign.lval = lval;
    e->as.compoundAssign.rval = rval;
    return e;
}

JStarExpr* jsrFunLiteral(int line, const JStarFormalArgs* args, bool isGenerator, JStarStmt* body) {
    JStarExpr* e = newExpr(line, JSR_FUN_LIT);
    e->as.funLit.func = jsrFuncDecl(line, &(JStarIdentifier){0}, args, isGenerator, body);
    return e;
}

JStarExpr* jsrSuperLiteral(int line, JStarTok* name, JStarExpr* args) {
    JStarExpr* e = newExpr(line, JSR_SUPER);
    e->as.sup.name = (JStarIdentifier){name->length, name->lexeme};
    e->as.sup.args = args;
    return e;
}

void jsrExprFree(JStarExpr* e) {
    if(e == NULL) return;

    switch(e->type) {
    case JSR_BINARY:
        jsrExprFree(e->as.binary.left);
        jsrExprFree(e->as.binary.right);
        break;
    case JSR_UNARY:
        jsrExprFree(e->as.unary.operand);
        break;
    case JSR_ASSIGN:
        jsrExprFree(e->as.assign.lval);
        jsrExprFree(e->as.assign.rval);
        break;
    case JSR_LIST:
        jsrExprFree(e->as.listLiteral.exprs);
        break;
    case JSR_TUPLE:
        jsrExprFree(e->as.tupleLiteral.exprs);
        break;
    case JSR_TABLE:
        jsrExprFree(e->as.tableLiteral.keyVals);
        break;
    case JSR_EXPR_LST: {
        ext_vec_foreach(JStarExpr** expr, e->as.exprList) {
            jsrExprFree(*expr);
        }
        ext_vec_free(e->as.exprList);
        break;
    }
    case JSR_CALL:
        jsrExprFree(e->as.call.callee);
        jsrExprFree(e->as.call.args);
        break;
    case JSR_PROPERTY_ACCESS:
        jsrExprFree(e->as.propertyAccess.left);
        break;
    case JSR_INDEX:
        jsrExprFree(e->as.index.left);
        jsrExprFree(e->as.index.index);
        break;
    case JSR_TERNARY:
        jsrExprFree(e->as.ternary.cond);
        jsrExprFree(e->as.ternary.thenExpr);
        jsrExprFree(e->as.ternary.elseExpr);
        break;
    case JSR_COMPOUND_ASSIGN:
        jsrExprFree(e->as.compoundAssign.lval);
        jsrExprFree(e->as.compoundAssign.rval);
        break;
    case JSR_FUN_LIT:
        jsrStmtFree(e->as.funLit.func);
        break;
    case JSR_SPREAD:
        jsrExprFree(e->as.spread.expr);
        break;
    case JSR_POWER:
        jsrExprFree(e->as.pow.base);
        jsrExprFree(e->as.pow.exp);
        break;
    case JSR_SUPER:
        jsrExprFree(e->as.sup.args);
        break;
    case JSR_YIELD:
        jsrExprFree(e->as.yield.expr);
        break;
    case JSR_NUMBER:
    case JSR_BOOL:
    case JSR_STRING:
    case JSR_VAR:
    case JSR_NULL:
        break;
    }

    free(e);
}

// -----------------------------------------------------------------------------
// STATEMENT NODES
// -----------------------------------------------------------------------------

static JStarStmt* newStmt(int line, JStarStmtType type) {
    JStarStmt* s = malloc(sizeof(*s));
    JSR_ASSERT(s, "Out of memory");
    s->line = line;
    s->type = type;
    return s;
}

static JStarStmt* newDecl(int line, JStarStmtType type) {
    JSR_ASSERT((type == JSR_VARDECL || type == JSR_FUNCDECL || type == JSR_CLASSDECL ||
                type == JSR_NATIVEDECL),
               "Not a declaration");
    JStarStmt* s = newStmt(line, type);
    JSR_ASSERT(s, "Out of memory");
    s->as.decl.isStatic = false;
    s->as.decl.decorators = NULL;
    return s;
}

// Declarations

JStarStmt* jsrFuncDecl(int line, const JStarIdentifier* name, const JStarFormalArgs* args,
                       bool isGenerator, JStarStmt* body) {
    JStarStmt* f = newDecl(line, JSR_FUNCDECL);
    f->as.decl.as.fun.id = *name;
    f->as.decl.as.fun.formalArgs = *args;
    f->as.decl.as.fun.body = body;
    f->as.decl.as.fun.isGenerator = isGenerator;
    return f;
}

JStarStmt* jsrNativeDecl(int line, const JStarIdentifier* name, const JStarFormalArgs* args) {
    JStarStmt* n = newDecl(line, JSR_NATIVEDECL);
    n->as.decl.as.native.id = *name;
    n->as.decl.as.native.formalArgs = *args;
    return n;
}

JStarStmt* jsrClassDecl(int line, const JStarIdentifier* clsName, JStarExpr* sup,
                        ext_vector(JStarStmt*) methods) {
    JStarStmt* c = newDecl(line, JSR_CLASSDECL);
    c->as.decl.as.cls.sup = sup;
    c->as.decl.as.cls.id = *clsName;
    c->as.decl.as.cls.methods = methods;
    return c;
}

JStarStmt* jsrVarDecl(int line, bool isUnpack, ext_vector(JStarIdentifier) ids, JStarExpr* init) {
    JStarStmt* s = newDecl(line, JSR_VARDECL);
    s->as.decl.as.var.ids = ids;
    s->as.decl.as.var.isUnpack = isUnpack;
    s->as.decl.as.var.init = init;
    return s;
}

// Control flow statements

JStarStmt* jsrWithStmt(int line, JStarExpr* e, const JStarIdentifier* varName, JStarStmt* block) {
    JStarStmt* w = newStmt(line, JSR_WITH);
    w->as.withStmt.e = e;
    w->as.withStmt.var = *varName;
    w->as.withStmt.block = block;
    return w;
}

JStarStmt* jsrForStmt(int line, JStarStmt* init, JStarExpr* cond, JStarExpr* act, JStarStmt* body) {
    JStarStmt* s = newStmt(line, JSR_FOR);
    s->as.forStmt.init = init;
    s->as.forStmt.cond = cond;
    s->as.forStmt.act = act;
    s->as.forStmt.body = body;
    return s;
}

JStarStmt* jsrForEachStmt(int line, JStarStmt* var, JStarExpr* iter, JStarStmt* body) {
    JStarStmt* s = newStmt(line, JSR_FOREACH);
    s->as.forEach.var = var;
    s->as.forEach.iterable = iter;
    s->as.forEach.body = body;
    return s;
}

JStarStmt* jsrWhileStmt(int line, JStarExpr* cond, JStarStmt* body) {
    JStarStmt* s = newStmt(line, JSR_WHILE);
    s->as.whileStmt.cond = cond;
    s->as.whileStmt.body = body;
    return s;
}

JStarStmt* jsrReturnStmt(int line, JStarExpr* e) {
    JStarStmt* s = newStmt(line, JSR_RETURN);
    s->as.returnStmt.e = e;
    return s;
}

JStarStmt* jsrIfStmt(int line, JStarExpr* cond, JStarStmt* thenStmt, JStarStmt* elseStmt) {
    JStarStmt* s = newStmt(line, JSR_IF);
    s->as.ifStmt.cond = cond;
    s->as.ifStmt.thenStmt = thenStmt;
    s->as.ifStmt.elseStmt = elseStmt;
    return s;
}

JStarStmt* jsrBlockStmt(int line, ext_vector(JStarStmt*) list) {
    JStarStmt* s = newStmt(line, JSR_BLOCK);
    s->as.blockStmt.stmts = list;
    return s;
}

JStarStmt* jsrImportStmt(int line, ext_vector(JStarIdentifier) modules,
                         ext_vector(JStarIdentifier) names, const JStarIdentifier* as) {
    JStarStmt* s = newStmt(line, JSR_IMPORT);
    s->as.importStmt.modules = modules;
    s->as.importStmt.names = names;
    s->as.importStmt.as = *as;
    return s;
}

JStarStmt* jsrExprStmt(int line, JStarExpr* e) {
    JStarStmt* s = newStmt(line, JSR_EXPR_STMT);
    s->as.exprStmt = e;
    return s;
}

JStarStmt* jsrTryStmt(int line, JStarStmt* blck, ext_vector(JStarStmt*) excs, JStarStmt* ensure) {
    JStarStmt* s = newStmt(line, JSR_TRY);
    s->as.tryStmt.block = blck;
    s->as.tryStmt.excs = excs;
    s->as.tryStmt.ensure = ensure;
    return s;
}

JStarStmt* jsrExceptStmt(int line, JStarExpr* cls, const JStarIdentifier* varName,
                         JStarStmt* block) {
    JStarStmt* s = newStmt(line, JSR_EXCEPT);
    s->as.excStmt.block = block;
    s->as.excStmt.cls = cls;
    s->as.excStmt.var = *varName;
    return s;
}

JStarStmt* jsrRaiseStmt(int line, JStarExpr* e) {
    JStarStmt* s = newStmt(line, JSR_RAISE);
    s->as.raiseStmt.exc = e;
    return s;
}

JStarStmt* jsrContinueStmt(int line) {
    JStarStmt* s = newStmt(line, JSR_CONTINUE);
    s->as.exprStmt = NULL;
    return s;
}

JStarStmt* jsrBreakStmt(int line) {
    JStarStmt* s = newStmt(line, JSR_BREAK);
    s->as.exprStmt = NULL;
    return s;
}

static void freeDeclaration(JStarStmt* s) {
    JStarStmtType type = s->type;
    JSR_ASSERT((type == JSR_VARDECL || type == JSR_FUNCDECL || type == JSR_CLASSDECL ||
                type == JSR_NATIVEDECL),
               "Not a declaration");
    (void)type;

    ext_vec_foreach(JStarExpr** e, s->as.decl.decorators) {
        jsrExprFree(*e);
    }

    ext_vec_free(s->as.decl.decorators);
}

void jsrStmtFree(JStarStmt* s) {
    if(s == NULL) return;

    switch(s->type) {
    case JSR_IF:
        jsrExprFree(s->as.ifStmt.cond);
        jsrStmtFree(s->as.ifStmt.thenStmt);
        jsrStmtFree(s->as.ifStmt.elseStmt);
        break;
    case JSR_FOR:
        jsrStmtFree(s->as.forStmt.init);
        jsrExprFree(s->as.forStmt.cond);
        jsrExprFree(s->as.forStmt.act);
        jsrStmtFree(s->as.forStmt.body);
        break;
    case JSR_FOREACH:
        jsrStmtFree(s->as.forEach.var);
        jsrExprFree(s->as.forEach.iterable);
        jsrStmtFree(s->as.forEach.body);
        break;
    case JSR_WHILE:
        jsrExprFree(s->as.whileStmt.cond);
        jsrStmtFree(s->as.whileStmt.body);
        break;
    case JSR_RETURN:
        jsrExprFree(s->as.returnStmt.e);
        break;
    case JSR_EXPR_STMT:
        jsrExprFree(s->as.exprStmt);
        break;
    case JSR_BLOCK: {
        ext_vec_foreach(JStarStmt** stmt, s->as.blockStmt.stmts) {
            jsrStmtFree(*stmt);
        }
        ext_vec_free(s->as.blockStmt.stmts);
        break;
    }
    case JSR_FUNCDECL: {
        freeDeclaration(s);
        ext_vec_foreach(JStarFormalArg * arg, s->as.decl.as.fun.formalArgs.args) {
            if(arg->type == UNPACK) {
                ext_vec_free(arg->as.unpack);
            }
        }
        ext_vec_free(s->as.decl.as.fun.formalArgs.args);
        ext_vec_foreach(JStarExpr** e, s->as.decl.as.fun.formalArgs.defaults) {
            jsrExprFree(*e);
        }
        ext_vec_free(s->as.decl.as.fun.formalArgs.defaults);
        jsrStmtFree(s->as.decl.as.fun.body);
        break;
    }
    case JSR_NATIVEDECL: {
        freeDeclaration(s);
        ext_vec_foreach(JStarFormalArg * arg, s->as.decl.as.fun.formalArgs.args) {
            if(arg->type == UNPACK) {
                ext_vec_free(arg->as.unpack);
            }
        }
        ext_vec_free(s->as.decl.as.fun.formalArgs.args);
        ext_vec_foreach(JStarExpr** e, s->as.decl.as.fun.formalArgs.defaults) {
            jsrExprFree(*e);
        }
        ext_vec_free(s->as.decl.as.fun.formalArgs.defaults);
        break;
    }
    case JSR_CLASSDECL: {
        freeDeclaration(s);
        jsrExprFree(s->as.decl.as.cls.sup);
        ext_vec_foreach(JStarStmt** stmt, s->as.decl.as.cls.methods) {
            jsrStmtFree(*stmt);
        }
        ext_vec_free(s->as.decl.as.cls.methods);
        break;
    }
    case JSR_VARDECL: {
        freeDeclaration(s);
        jsrExprFree(s->as.decl.as.var.init);
        ext_vec_free(s->as.decl.as.var.ids);
        break;
    }
    case JSR_TRY:
        jsrStmtFree(s->as.tryStmt.block);
        jsrStmtFree(s->as.tryStmt.ensure);
        ext_vec_foreach(JStarStmt** stmt, s->as.tryStmt.excs) {
            jsrStmtFree(*stmt);
        }
        ext_vec_free(s->as.tryStmt.excs);
        break;
    case JSR_EXCEPT:
        jsrExprFree(s->as.excStmt.cls);
        jsrStmtFree(s->as.excStmt.block);
        break;
    case JSR_RAISE:
        jsrExprFree(s->as.raiseStmt.exc);
        break;
    case JSR_WITH:
        jsrExprFree(s->as.withStmt.e);
        jsrStmtFree(s->as.withStmt.block);
        break;
    case JSR_IMPORT: {
        ext_vec_free(s->as.importStmt.modules);
        ext_vec_free(s->as.importStmt.names);
        break;
    }
    case JSR_CONTINUE:
    case JSR_BREAK:
        break;
    }

    free(s);
}
