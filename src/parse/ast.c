#include "parse/ast.h"

#include <string.h>

#include "parse/lex.h"
#include "parse/vector.h"

JStarIdentifier* jsrNewIdentifier(size_t length, const char* name) {
    JStarIdentifier* id = malloc(sizeof(*id));
    id->length = length;
    id->name = name;
    return id;
}

bool jsrIdentifierEq(JStarIdentifier* id1, JStarIdentifier* id2) {
    return id1->length == id2->length && (memcmp(id1->name, id2->name, id1->length) == 0);
}

// -----------------------------------------------------------------------------
// EXPRESSION NODES
// -----------------------------------------------------------------------------

static JStarExpr* newExpr(int line, JStarExprType type) {
    JStarExpr* e = malloc(sizeof(*e));
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
    e->as.string.str = str;
    e->as.string.length = len;
    return e;
}

JStarExpr* jsrVarLiteral(int line, const char* var, size_t len) {
    JStarExpr* e = newExpr(line, JSR_VAR);
    e->as.var.id.name = var;
    e->as.var.id.length = len;
    return e;
}

JStarExpr* jsrArrLiteral(int line, JStarExpr* exprs) {
    JStarExpr* a = newExpr(line, JSR_ARRAY);
    a->as.array.exprs = exprs;
    return a;
}

JStarExpr* jsrTupleLiteral(int line, JStarExpr* exprs) {
    JStarExpr* a = newExpr(line, JSR_TUPLE);
    a->as.tuple.exprs = exprs;
    return a;
}

JStarExpr* jsrTableLiteral(int line, JStarExpr* keyVals) {
    JStarExpr* t = newExpr(line, JSR_TABLE);
    t->as.table.keyVals = keyVals;
    return t;
}

JStarExpr* jsrExprList(int line, Vector* exprs) {
    JStarExpr* e = newExpr(line, JSR_EXPR_LST);
    e->as.list = vecMove(exprs);
    return e;
}

JStarExpr* jsrCallExpr(int line, JStarExpr* callee, JStarExpr* args, bool unpackArg) {
    JStarExpr* e = newExpr(line, JSR_CALL);
    e->as.call.unpackArg = unpackArg;
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

JStarExpr* jsrAccessExpr(int line, JStarExpr* left, const char* name, size_t length) {
    JStarExpr* e = newExpr(line, JSR_ACCESS);
    e->as.access.left = left;
    e->as.access.id.name = name;
    e->as.access.id.length = length;
    return e;
}

JStarExpr* jsrArrayAccExpr(int line, JStarExpr* left, JStarExpr* index) {
    JStarExpr* e = newExpr(line, JSR_ARR_ACCESS);
    e->as.arrayAccess.left = left;
    e->as.arrayAccess.index = index;
    return e;
}

JStarExpr* jsrTernaryExpr(int line, JStarExpr* cond, JStarExpr* thenExpr, JStarExpr* elseExpr) {
    JStarExpr* e = newExpr(line, JSR_TERNARY);
    e->as.ternary.cond = cond;
    e->as.ternary.thenExpr = thenExpr;
    e->as.ternary.elseExpr = elseExpr;
    return e;
}

JStarExpr* jsrCompundAssExpr(int line, JStarTokType op, JStarExpr* lval, JStarExpr* rval) {
    JStarExpr* e = newExpr(line, JSR_COMPUND_ASS);
    e->as.compound.op = op;
    e->as.compound.lval = lval;
    e->as.compound.rval = rval;
    return e;
}

JStarExpr* jsrFuncLiteral(int line, Vector* args, Vector* defArgs, bool vararg, JStarStmt* body) {
    JStarExpr* e = newExpr(line, JSR_FUNC_LIT);
    JStarTok name = {0};  // Empty name
    e->as.funLit.func = jsrFuncDecl(line, &name, args, defArgs, vararg, body);
    return e;
}

JStarExpr* jsrSuperLiteral(int line, JStarTok* name, JStarExpr* args, bool unpackArg) {
    JStarExpr* e = newExpr(line, JSR_SUPER);
    e->as.sup.name.name = name->lexeme;
    e->as.sup.name.length = name->length;
    e->as.sup.unpackArg = unpackArg;
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
    case JSR_ARRAY:
        jsrExprFree(e->as.array.exprs);
        break;
    case JSR_TUPLE:
        jsrExprFree(e->as.tuple.exprs);
        break;
    case JSR_TABLE:
        jsrExprFree(e->as.table.keyVals);
        break;
    case JSR_EXPR_LST: {
        vecForeach(JStarExpr** expr, e->as.list) { 
            jsrExprFree(*expr); 
        }
        vecFree(&e->as.list);
        break;
    }
    case JSR_CALL:
        jsrExprFree(e->as.call.callee);
        jsrExprFree(e->as.call.args);
        break;
    case JSR_ACCESS:
        jsrExprFree(e->as.access.left);
        break;
    case JSR_ARR_ACCESS:
        jsrExprFree(e->as.arrayAccess.left);
        jsrExprFree(e->as.arrayAccess.index);
        break;
    case JSR_TERNARY:
        jsrExprFree(e->as.ternary.cond);
        jsrExprFree(e->as.ternary.thenExpr);
        jsrExprFree(e->as.ternary.elseExpr);
        break;
    case JSR_COMPUND_ASS:
        jsrExprFree(e->as.compound.lval);
        jsrExprFree(e->as.compound.rval);
        break;
    case JSR_FUNC_LIT:
        jsrStmtFree(e->as.funLit.func);
        break;
    case JSR_POWER:
        jsrExprFree(e->as.pow.base);
        jsrExprFree(e->as.pow.exp);
        break;
    case JSR_SUPER:
        jsrExprFree(e->as.sup.args);
        break;
    default:
        break;
    }

    free(e);
}

// -----------------------------------------------------------------------------
// STATEMENT NODES
// -----------------------------------------------------------------------------

static JStarStmt* newStmt(int line, StmtType type) {
    JStarStmt* s = malloc(sizeof(*s));
    s->line = line;
    s->type = type;
    return s;
}

JStarStmt* jsrFuncDecl(int line, JStarTok* name, Vector* args, Vector* defArgs, bool vararg,
                       JStarStmt* body) {
    JStarStmt* f = newStmt(line, JSR_FUNCDECL);
    f->as.funcDecl.id.name = name->lexeme;
    f->as.funcDecl.id.length = name->length;
    f->as.funcDecl.formalArgs = vecMove(args);
    f->as.funcDecl.defArgs = vecMove(defArgs);
    f->as.funcDecl.isVararg = vararg;
    f->as.funcDecl.isStatic = false;
    f->as.funcDecl.body = body;
    return f;
}

JStarStmt* jsrNativeDecl(int line, JStarTok* name, Vector* args, Vector* defArgs, bool vararg) {
    JStarStmt* n = newStmt(line, JSR_NATIVEDECL);
    n->as.nativeDecl.id.name = name->lexeme;
    n->as.nativeDecl.id.length = name->length;
    n->as.nativeDecl.formalArgs = vecMove(args);
    n->as.nativeDecl.isVararg = vararg;
    n->as.nativeDecl.isStatic = false;
    n->as.nativeDecl.defArgs = vecMove(defArgs);
    return n;
}

JStarStmt* jsrClassDecl(int line, JStarTok* clsName, JStarExpr* sup, Vector* methods) {
    JStarStmt* c = newStmt(line, JSR_CLASSDECL);
    c->as.classDecl.sup = sup;
    c->as.classDecl.isStatic = false;
    c->as.classDecl.id.name = clsName->lexeme;
    c->as.classDecl.id.length = clsName->length;
    c->as.classDecl.methods = vecMove(methods);
    return c;
}

JStarStmt* jsrVarDecl(int line, bool isUnpack, Vector* ids, JStarExpr* init) {
    JStarStmt* s = newStmt(line, JSR_VARDECL);
    s->as.varDecl.ids = vecMove(ids);
    s->as.varDecl.isUnpack = isUnpack;
    s->as.varDecl.isStatic = false;
    s->as.varDecl.init = init;
    return s;
}

JStarStmt* jsrWithStmt(int line, JStarExpr* e, JStarTok* varName, JStarStmt* block) {
    JStarStmt* w = newStmt(line, JSR_WITH);
    w->as.withStmt.e = e;
    w->as.withStmt.var.name = varName->lexeme;
    w->as.withStmt.var.length = varName->length;
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

JStarStmt* jsrBlockStmt(int line, Vector* list) {
    JStarStmt* s = newStmt(line, JSR_BLOCK);
    s->as.blockStmt.stmts = vecMove(list);
    return s;
}

JStarStmt* jsrImportStmt(int line, Vector* modules, Vector* impNames, JStarTok* as) {
    JStarStmt* s = newStmt(line, JSR_IMPORT);
    s->as.importStmt.modules = vecMove(modules);
    s->as.importStmt.impNames = vecMove(impNames);
    s->as.importStmt.as.name = as->lexeme;
    s->as.importStmt.as.length = as->length;
    return s;
}

JStarStmt* jsrExprStmt(int line, JStarExpr* e) {
    JStarStmt* s = newStmt(line, JSR_EXPR_STMT);
    s->as.exprStmt = e;
    return s;
}

JStarStmt* jsrTryStmt(int line, JStarStmt* blck, Vector* excs, JStarStmt* ensure) {
    JStarStmt* s = newStmt(line, JSR_TRY);
    s->as.tryStmt.block = blck;
    s->as.tryStmt.excs = vecMove(excs);
    s->as.tryStmt.ensure = ensure;
    return s;
}

JStarStmt* jsrExceptStmt(int line, JStarExpr* cls, JStarTok* varName, JStarStmt* block) {
    JStarStmt* s = newStmt(line, JSR_EXCEPT);
    s->as.excStmt.block = block;
    s->as.excStmt.cls = cls;
    s->as.excStmt.var.length = varName->length;
    s->as.excStmt.var.name = varName->lexeme;
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
        vecForeach(JStarStmt** stmt, s->as.blockStmt.stmts) {
            jsrStmtFree(*stmt);
        }
        vecFree(&s->as.blockStmt.stmts);
        break;
    }
    case JSR_FUNCDECL: {
        vecForeach(JStarIdentifier** id, s->as.funcDecl.formalArgs) {
            free(*id);
        }
        vecForeach(JStarExpr** e, s->as.funcDecl.defArgs) {
            jsrExprFree(*e);
        }
        vecFree(&s->as.funcDecl.formalArgs);
        vecFree(&s->as.funcDecl.defArgs);
        jsrStmtFree(s->as.funcDecl.body);
        break;
    }
    case JSR_NATIVEDECL: {
        vecForeach(JStarIdentifier** id, s->as.nativeDecl.formalArgs) {
            free(*id);
        }
        vecForeach(JStarExpr** e, s->as.nativeDecl.defArgs) {
            jsrExprFree(*e);
        }
        vecFree(&s->as.nativeDecl.formalArgs);
        vecFree(&s->as.nativeDecl.defArgs);
        break;
    }
    case JSR_CLASSDECL: {
        jsrExprFree(s->as.classDecl.sup);
        vecForeach(JStarStmt** stmt, s->as.classDecl.methods) {
            jsrStmtFree(*stmt);
        }
        vecFree(&s->as.classDecl.methods);
        break;
    }
    case JSR_VARDECL: {
        jsrExprFree(s->as.varDecl.init);
        vecForeach(JStarIdentifier** id, s->as.varDecl.ids) {
            free(*id);
        }
        vecFree(&s->as.varDecl.ids);
        break;
    }
    case JSR_TRY:
        jsrStmtFree(s->as.tryStmt.block);
        jsrStmtFree(s->as.tryStmt.ensure);
        vecForeach(JStarStmt** stmt, s->as.tryStmt.excs) {
            jsrStmtFree(*stmt);
        }
        vecFree(&s->as.tryStmt.excs);
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
        vecForeach(JStarIdentifier** id, s->as.importStmt.modules) {
            free(*id);
        }
        vecForeach(JStarIdentifier** id, s->as.importStmt.impNames) {
            free(*id);
        }
        vecFree(&s->as.importStmt.modules);
        vecFree(&s->as.importStmt.impNames);
        break;
    }
    case JSR_CONTINUE:
    case JSR_BREAK:
        break;
    }

    free(s);
}
