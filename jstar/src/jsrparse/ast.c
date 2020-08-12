#include "jsrparse/ast.h"

#include <string.h>

Identifier* newIdentifier(size_t length, const char* name) {
    Identifier* id = malloc(sizeof(*id));
    id->length = length;
    id->name = name;
    return id;
}

bool identifierEquals(Identifier* id1, Identifier* id2) {
    return id1->length == id2->length && (memcmp(id1->name, id2->name, id1->length) == 0);
}

//----- Expressions -----

static Expr* newExpr(int line, ExprType type) {
    Expr* e = malloc(sizeof(*e));
    e->line = line;
    e->type = type;
    return e;
}

Expr* newBinary(int line, TokenType op, Expr* l, Expr* r) {
    Expr* e = newExpr(line, BINARY);
    e->as.binary.op = op;
    e->as.binary.left = l;
    e->as.binary.right = r;
    return e;
}

Expr* newAssign(int line, Expr* lval, Expr* rval) {
    Expr* e = newExpr(line, ASSIGN);
    e->as.assign.lval = lval;
    e->as.assign.rval = rval;
    return e;
}

Expr* newUnary(int line, TokenType op, Expr* operand) {
    Expr* e = newExpr(line, UNARY);
    e->as.unary.op = op;
    e->as.unary.operand = operand;
    return e;
}

Expr* newNullLiteral(int line) {
    Expr* e = newExpr(line, NULL_LIT);
    return e;
}

Expr* newNumLiteral(int line, double num) {
    Expr* e = newExpr(line, NUM_LIT);
    e->as.num = num;
    return e;
}

Expr* newBoolLiteral(int line, bool boolean) {
    Expr* e = newExpr(line, BOOL_LIT);
    e->as.boolean = boolean;
    return e;
}

Expr* newStrLiteral(int line, const char* str, size_t len) {
    Expr* e = newExpr(line, STR_LIT);
    e->as.string.str = str;
    e->as.string.length = len;
    return e;
}

Expr* newVarLiteral(int line, const char* var, size_t len) {
    Expr* e = newExpr(line, VAR_LIT);
    e->as.var.id.name = var;
    e->as.var.id.length = len;
    return e;
}

Expr* newArrLiteral(int line, Expr* exprs) {
    Expr* a = newExpr(line, ARR_LIT);
    a->as.array.exprs = exprs;
    return a;
}

Expr* newTupleLiteral(int line, Expr* exprs) {
    Expr* a = newExpr(line, TUPLE_LIT);
    a->as.tuple.exprs = exprs;
    return a;
}

Expr* newTableLiteral(int line, Expr* keyVals) {
    Expr* t = newExpr(line, TABLE_LIT);
    t->as.table.keyVals = keyVals;
    return t;
}

Expr* newExprList(int line, Vector* exprs) {
    Expr* e = newExpr(line, EXPR_LST);
    e->as.list = vecMove(exprs);
    return e;
}

Expr* newCallExpr(int line, Expr* callee, Expr* args) {
    Expr* e = newExpr(line, CALL_EXPR);
    e->as.call.callee = callee;
    e->as.call.args = args;
    return e;
}

Expr* newExpExpr(int line, Expr* base, Expr* exp) {
    Expr* e = newExpr(line, EXP_EXPR);
    e->as.exponent.base = base;
    e->as.exponent.exp = exp;
    return e;
}

Expr* newAccessExpr(int line, Expr* left, const char* name, size_t length) {
    Expr* e = newExpr(line, ACCESS_EXPR);
    e->as.access.left = left;
    e->as.access.id.name = name;
    e->as.access.id.length = length;
    return e;
}

Expr* newArrayAccExpr(int line, Expr* left, Expr* index) {
    Expr* e = newExpr(line, ARR_ACC);
    e->as.arrayAccess.left = left;
    e->as.arrayAccess.index = index;
    return e;
}

Expr* newTernary(int line, Expr* cond, Expr* thenExpr, Expr* elseExpr) {
    Expr* e = newExpr(line, TERNARY);
    e->as.ternary.cond = cond;
    e->as.ternary.thenExpr = thenExpr;
    e->as.ternary.elseExpr = elseExpr;
    return e;
}

Expr* newCompoundAssing(int line, TokenType op, Expr* lval, Expr* rval) {
    Expr* e = newExpr(line, COMP_ASSIGN);
    e->as.compound.op = op;
    e->as.compound.lval = lval;
    e->as.compound.rval = rval;
    return e;
}

Expr* newFunLit(int line, Vector* args, Vector* defArgs, bool vararg, Stmt* body) {
    Expr* e = newExpr(line, FUN_LIT);
    Token name = {0};  // Empty name
    e->as.funLit.func = newFuncDecl(line, &name, args, defArgs, vararg, body);
    return e;
}

Expr* newSuperLiteral(int line, Token* name, Expr* args) {
    Expr* e = newExpr(line, SUPER_LIT);
    e->as.sup.name.name = name->lexeme;
    e->as.sup.name.length = name->length;
    e->as.sup.args = args;
    return e;
}

void freeExpr(Expr* e) {
    if(e == NULL) return;

    switch(e->type) {
    case BINARY:
        freeExpr(e->as.binary.left);
        freeExpr(e->as.binary.right);
        break;
    case UNARY:
        freeExpr(e->as.unary.operand);
        break;
    case ASSIGN:
        freeExpr(e->as.assign.lval);
        freeExpr(e->as.assign.rval);
        break;
    case ARR_LIT:
        freeExpr(e->as.array.exprs);
        break;
    case TUPLE_LIT:
        freeExpr(e->as.tuple.exprs);
        break;
    case TABLE_LIT:
        freeExpr(e->as.table.keyVals);
        break;
    case EXPR_LST: {
        // clang-format off
        vecForeach(Expr** expr, e->as.list) { 
            freeExpr(*expr); 
        }
        // clang-format on
        vecFree(&e->as.list);
        break;
    }
    case CALL_EXPR:
        freeExpr(e->as.call.callee);
        freeExpr(e->as.call.args);
        break;
    case ACCESS_EXPR:
        freeExpr(e->as.access.left);
        break;
    case ARR_ACC:
        freeExpr(e->as.arrayAccess.left);
        freeExpr(e->as.arrayAccess.index);
        break;
    case TERNARY:
        freeExpr(e->as.ternary.cond);
        freeExpr(e->as.ternary.thenExpr);
        freeExpr(e->as.ternary.elseExpr);
        break;
    case COMP_ASSIGN:
        freeExpr(e->as.compound.lval);
        freeExpr(e->as.compound.rval);
        break;
    case FUN_LIT:
        freeStmt(e->as.funLit.func);
        break;
    case EXP_EXPR:
        freeExpr(e->as.exponent.base);
        freeExpr(e->as.exponent.exp);
        break;
    case SUPER_LIT:
        freeExpr(e->as.sup.args);
        break;
    default:
        break;
    }

    free(e);
}

//----- Statements -----

static Stmt* newStmt(int line, StmtType type) {
    Stmt* s = malloc(sizeof(*s));
    s->line = line;
    s->type = type;
    return s;
}

Stmt* newFuncDecl(int line, Token* name, Vector* args, Vector* defArgs, bool vararg, Stmt* body) {
    Stmt* f = newStmt(line, FUNCDECL);
    f->as.funcDecl.id.name = name->lexeme;
    f->as.funcDecl.id.length = name->length;
    f->as.funcDecl.formalArgs = vecMove(args);
    f->as.funcDecl.defArgs = vecMove(defArgs);
    f->as.funcDecl.isVararg = vararg;
    f->as.funcDecl.body = body;
    return f;
}

Stmt* newNativeDecl(int line, Token* name, Vector* args, Vector* defArgs, bool vararg) {
    Stmt* n = newStmt(line, NATIVEDECL);
    n->as.nativeDecl.id.name = name->lexeme;
    n->as.nativeDecl.id.length = name->length;
    n->as.nativeDecl.formalArgs = vecMove(args);
    n->as.nativeDecl.isVararg = vararg;
    n->as.nativeDecl.defArgs = vecMove(defArgs);
    return n;
}

Stmt* newClassDecl(int line, Token* clsName, Expr* sup, Vector* methods) {
    Stmt* c = newStmt(line, CLASSDECL);
    c->as.classDecl.sup = sup;
    c->as.classDecl.id.name = clsName->lexeme;
    c->as.classDecl.id.length = clsName->length;
    c->as.classDecl.methods = vecMove(methods);
    return c;
}

Stmt* newWithStmt(int line, Expr* e, Token* varName, Stmt* block) {
    Stmt* w = newStmt(line, WITH_STMT);
    w->as.withStmt.e = e;
    w->as.withStmt.var.name = varName->lexeme;
    w->as.withStmt.var.length = varName->length;
    w->as.withStmt.block = block;
    return w;
}

Stmt* newForStmt(int line, Stmt* init, Expr* cond, Expr* act, Stmt* body) {
    Stmt* s = newStmt(line, FOR);
    s->as.forStmt.init = init;
    s->as.forStmt.cond = cond;
    s->as.forStmt.act = act;
    s->as.forStmt.body = body;
    return s;
}

Stmt* newForEach(int line, Stmt* var, Expr* iter, Stmt* body) {
    Stmt* s = newStmt(line, FOREACH);
    s->as.forEach.var = var;
    s->as.forEach.iterable = iter;
    s->as.forEach.body = body;
    return s;
}

Stmt* newVarDecl(int line, bool isUnpack, Vector* ids, Expr* init) {
    Stmt* s = newStmt(line, VARDECL);
    s->as.varDecl.ids = vecMove(ids);
    s->as.varDecl.isUnpack = isUnpack;
    s->as.varDecl.init = init;
    return s;
}

Stmt* newWhileStmt(int line, Expr* cond, Stmt* body) {
    Stmt* s = newStmt(line, WHILE);
    s->as.whileStmt.cond = cond;
    s->as.whileStmt.body = body;
    return s;
}

Stmt* newReturnStmt(int line, Expr* e) {
    Stmt* s = newStmt(line, RETURN_STMT);
    s->as.returnStmt.e = e;
    return s;
}

Stmt* newIfStmt(int line, Expr* cond, Stmt* thenStmt, Stmt* elseStmt) {
    Stmt* s = newStmt(line, IF);
    s->as.ifStmt.cond = cond;
    s->as.ifStmt.thenStmt = thenStmt;
    s->as.ifStmt.elseStmt = elseStmt;
    return s;
}

Stmt* newBlockStmt(int line, Vector* list) {
    Stmt* s = newStmt(line, BLOCK);
    s->as.blockStmt.stmts = vecMove(list);
    return s;
}

Stmt* newImportStmt(int line, Vector* modules, Vector* impNames, Token* as) {
    Stmt* s = newStmt(line, IMPORT);
    s->as.importStmt.modules = vecMove(modules);
    s->as.importStmt.impNames = vecMove(impNames);
    s->as.importStmt.as.name = as->lexeme;
    s->as.importStmt.as.length = as->length;
    return s;
}

Stmt* newExprStmt(int line, Expr* e) {
    Stmt* s = newStmt(line, EXPR);
    s->as.exprStmt = e;
    return s;
}

Stmt* newTryStmt(int line, Stmt* blck, Vector* excs, Stmt* ensure) {
    Stmt* s = newStmt(line, TRY_STMT);
    s->as.tryStmt.block = blck;
    s->as.tryStmt.excs = vecMove(excs);
    s->as.tryStmt.ensure = ensure;
    return s;
}

Stmt* newExceptStmt(int line, Expr* cls, Token* varName, Stmt* block) {
    Stmt* s = newStmt(line, EXCEPT_STMT);
    s->as.excStmt.block = block;
    s->as.excStmt.cls = cls;
    s->as.excStmt.var.length = varName->length;
    s->as.excStmt.var.name = varName->lexeme;
    return s;
}

Stmt* newRaiseStmt(int line, Expr* e) {
    Stmt* s = newStmt(line, RAISE_STMT);
    s->as.raiseStmt.exc = e;
    return s;
}

Stmt* newContinueStmt(int line) {
    Stmt* s = newStmt(line, CONTINUE_STMT);
    s->as.exprStmt = NULL;
    return s;
}

Stmt* newBreakStmt(int line) {
    Stmt* s = newStmt(line, BREAK_STMT);
    s->as.exprStmt = NULL;
    return s;
}

void freeStmt(Stmt* s) {
    if(s == NULL) return;

    switch(s->type) {
    case IF:
        freeExpr(s->as.ifStmt.cond);
        freeStmt(s->as.ifStmt.thenStmt);
        freeStmt(s->as.ifStmt.elseStmt);
        break;
    case FOR:
        freeStmt(s->as.forStmt.init);
        freeExpr(s->as.forStmt.cond);
        freeExpr(s->as.forStmt.act);
        freeStmt(s->as.forStmt.body);
        break;
    case FOREACH:
        freeStmt(s->as.forEach.var);
        freeExpr(s->as.forEach.iterable);
        freeStmt(s->as.forEach.body);
        break;
    case WHILE:
        freeExpr(s->as.whileStmt.cond);
        freeStmt(s->as.whileStmt.body);
        break;
    case RETURN_STMT:
        freeExpr(s->as.returnStmt.e);
        break;
    case EXPR:
        freeExpr(s->as.exprStmt);
        break;
    case BLOCK: {
        // clang-format off
        vecForeach(Stmt** stmt, s->as.blockStmt.stmts) {
            freeStmt(*stmt);
        }
        // clang-format on
        vecFree(&s->as.blockStmt.stmts);
        break;
    }
    case FUNCDECL: {
        // clang-format off
        vecForeach(Identifier** id, s->as.funcDecl.formalArgs) {
            free(*id);
        }
        vecForeach(Expr** e, s->as.funcDecl.defArgs) {
            freeExpr(*e);
        }
        // clang-format on
        vecFree(&s->as.funcDecl.formalArgs);
        vecFree(&s->as.funcDecl.defArgs);
        freeStmt(s->as.funcDecl.body);
        break;
    }
    case NATIVEDECL: {
        // clang-format off
        vecForeach(Identifier** id, s->as.nativeDecl.formalArgs) {
            free(*id);
        }
        vecForeach(Expr** e, s->as.nativeDecl.defArgs) {
            freeExpr(*e);
        }
        // clang-format on
        vecFree(&s->as.nativeDecl.formalArgs);
        vecFree(&s->as.nativeDecl.defArgs);
        break;
    }
    case CLASSDECL: {
        freeExpr(s->as.classDecl.sup);
        // clang-format off
        vecForeach(Stmt** stmt, s->as.classDecl.methods) {
            freeStmt(*stmt);
        }
        // clang-format on
        vecFree(&s->as.classDecl.methods);
        break;
    }
    case VARDECL: {
        freeExpr(s->as.varDecl.init);
        // clang-format off
        vecForeach(Identifier** id, s->as.varDecl.ids) {
            free(*id);
        }
        // clang-format on
        vecFree(&s->as.varDecl.ids);
        break;
    }
    case TRY_STMT:
        freeStmt(s->as.tryStmt.block);
        freeStmt(s->as.tryStmt.ensure);
        // clang-format off
        vecForeach(Stmt** stmt, s->as.tryStmt.excs) {
            freeStmt(*stmt);
        }
        // clang-format on
        vecFree(&s->as.tryStmt.excs);
        break;
    case EXCEPT_STMT:
        freeExpr(s->as.excStmt.cls);
        freeStmt(s->as.excStmt.block);
        break;
    case RAISE_STMT:
        freeExpr(s->as.raiseStmt.exc);
        break;
    case WITH_STMT:
        freeExpr(s->as.withStmt.e);
        freeStmt(s->as.withStmt.block);
        break;
    case IMPORT: {
        // clang-format off
        vecForeach(Identifier** id, s->as.importStmt.modules) {
            free(*id);
        }
        vecForeach(Identifier** id, s->as.importStmt.impNames) {
            free(*id);
        }
        // clang-format on
        vecFree(&s->as.importStmt.modules);
        vecFree(&s->as.importStmt.impNames);
        break;
    }
    case CONTINUE_STMT:
    case BREAK_STMT:
        break;
    }

    free(s);
}
