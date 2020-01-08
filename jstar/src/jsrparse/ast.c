#include "ast.h"

#include <string.h>

Identifier *newIdentifier(size_t length, const char *name) {
    Identifier *id = malloc(sizeof(*id));
    id->length = length;
    id->name = name;
    return id;
}

bool identifierEquals(Identifier *id1, Identifier *id2) {
    return id1->length == id2->length && (memcmp(id1->name, id2->name, id1->length) == 0);
}

//----- Expressions -----

static Expr *newExpr(int line, ExprType type) {
    Expr *e = malloc(sizeof(*e));
    e->line = line;
    e->type = type;
    return e;
}

Expr *newBinary(int line, Operator op, Expr *l, Expr *r) {
    Expr *e = newExpr(line, BINARY);
    e->as.binary.op = op;
    e->as.binary.left = l;
    e->as.binary.right = r;
    return e;
}

Expr *newAssign(int line, Expr *lval, Expr *rval) {
    Expr *e = newExpr(line, ASSIGN);
    e->as.assign.lval = lval;
    e->as.assign.rval = rval;
    return e;
}

Expr *newUnary(int line, Operator op, Expr *operand) {
    Expr *e = newExpr(line, UNARY);
    e->as.unary.op = op;
    e->as.unary.operand = operand;
    return e;
}

Expr *newNullLiteral(int line) {
    Expr *e = newExpr(line, NULL_LIT);
    return e;
}

Expr *newNumLiteral(int line, double num) {
    Expr *e = newExpr(line, NUM_LIT);
    e->as.num = num;
    return e;
}

Expr *newBoolLiteral(int line, bool boolean) {
    Expr *e = newExpr(line, BOOL_LIT);
    e->as.boolean = boolean;
    return e;
}

Expr *newStrLiteral(int line, const char *str, size_t len) {
    Expr *e = newExpr(line, STR_LIT);
    e->as.string.str = str;
    e->as.string.length = len;
    return e;
}

Expr *newCmdLiteral(int line, const char *cmd, size_t len) {
    Expr *e = newExpr(line, CMD_LIT);
    e->as.string.str = cmd;
    e->as.string.length = len;
    return e;
}

Expr *newVarLiteral(int line, const char *var, size_t len) {
    Expr *e = newExpr(line, VAR_LIT);
    e->as.var.id.name = var;
    e->as.var.id.length = len;
    return e;
}

Expr *newArrLiteral(int line, Expr *exprs) {
    Expr *a = newExpr(line, ARR_LIT);
    a->as.array.exprs = exprs;
    return a;
}

Expr *newTupleLiteral(int line, Expr *exprs) {
    Expr *a = newExpr(line, TUPLE_LIT);
    a->as.tuple.exprs = exprs;
    return a;
}

Expr *newTableLiteral(int line, Expr *keyVals) {
    Expr *t = newExpr(line, TABLE_LIT);
    t->as.table.keyVals = keyVals;
    return t;
}

Expr *newExprList(int line, LinkedList *exprs) {
    Expr *e = newExpr(line, EXPR_LST);
    e->as.list.lst = exprs;
    return e;
}

Expr *newCallExpr(int line, Expr *callee, LinkedList *args) {
    Expr *e = newExpr(line, CALL_EXPR);
    e->as.call.callee = callee;
    e->as.call.args = newExprList(line, args);
    return e;
}

Expr *newExpExpr(int line, Expr *base, Expr *exp) {
    Expr *e = newExpr(line, EXP_EXPR);
    e->as.exponent.base = base;
    e->as.exponent.exp = exp;
    return e;
}

Expr *newSuperLiteral(int line) {
    Expr *e = newExpr(line, SUPER_LIT);
    e->as.num = 0;
    return e;
}

Expr *newAccessExpr(int line, Expr *left, const char *name, size_t length) {
    Expr *e = newExpr(line, ACCESS_EXPR);
    e->as.access.left = left;
    e->as.access.id.name = name;
    e->as.access.id.length = length;
    return e;
}

Expr *newArrayAccExpr(int line, Expr *left, Expr *index) {
    Expr *e = newExpr(line, ARR_ACC);
    e->as.arrayAccess.left = left;
    e->as.arrayAccess.index = index;
    return e;
}

Expr *newTernary(int line, Expr *cond, Expr *thenExpr, Expr *elseExpr) {
    Expr *e = newExpr(line, TERNARY);
    e->as.ternary.cond = cond;
    e->as.ternary.thenExpr = thenExpr;
    e->as.ternary.elseExpr = elseExpr;
    return e;
}

Expr *newCompoundAssing(int line, Operator op, Expr *lval, Expr *rval) {
    Expr *e = newExpr(line, COMP_ASSIGN);
    e->as.compound.op = op;
    e->as.compound.lval = lval;
    e->as.compound.rval = rval;
    return e;
}

Expr *newAnonymousFunc(int line, bool vararg, LinkedList *args, LinkedList *defArgs, Stmt *body) {
    Expr *e = newExpr(line, ANON_FUNC);
    e->as.anonFunc.func = newFuncDecl(line, vararg, 0, NULL, args, defArgs, body);
    return e;
}

void freeExpr(Expr *e) {
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
        LinkedList *head = e->as.list.lst;
        while(head != NULL) {
            LinkedList *f = head;
            head = head->next;
            freeExpr(f->elem);
            free(f);
        }
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
    case ANON_FUNC:
        freeStmt(e->as.anonFunc.func);
        break;
    case EXP_EXPR:
        freeExpr(e->as.exponent.base);
        freeExpr(e->as.exponent.exp);
        break;
    default:
        break;
    }

    free(e);
}

//----- Statements -----

static Stmt *newStmt(int line, StmtType type) {
    Stmt *s = malloc(sizeof(*s));
    s->line = line;
    s->type = type;
    return s;
}

Stmt *newFuncDecl(int line, bool vararg, size_t length, const char *id, 
    LinkedList *args, LinkedList *defArgs, Stmt *body)
{
    Stmt *f = newStmt(line, FUNCDECL);
    f->funcDecl.id.name = id;
    f->funcDecl.id.length = length;
    f->funcDecl.formalArgs = args;
    f->funcDecl.defArgs = defArgs;
    f->funcDecl.isVararg = vararg;
    f->funcDecl.body = body;
    return f;
}

Stmt *newNativeDecl(int line, bool vararg, size_t length, const char *id, 
    LinkedList *args, LinkedList *defArgs)
{
    Stmt *n = newStmt(line, NATIVEDECL);
    n->nativeDecl.id.name = id;
    n->nativeDecl.id.length = length;
    n->nativeDecl.formalArgs = args;
    n->nativeDecl.isVararg = vararg;
    n->nativeDecl.defArgs = defArgs;
    return n;
}

Stmt *newClassDecl(int line, size_t clength, const char *cid, Expr *sup, LinkedList *methods) {
    Stmt *c = newStmt(line, CLASSDECL);
    c->classDecl.sup = sup;
    c->classDecl.id.name = cid;
    c->classDecl.id.length = clength;
    c->classDecl.methods = methods;
    return c;
}

Stmt *newWithStmt(int line, Expr *e, size_t varLen, const char *varName, Stmt *block) {
    Stmt *w = newStmt(line, WITH_STMT);
    w->withStmt.e = e;
    w->withStmt.var.length = varLen;
    w->withStmt.var.name = varName;
    w->withStmt.block = block;
    return w;
}


Stmt *newForStmt(int line, Stmt *init, Expr *cond, Expr *act, Stmt *body) {
    Stmt *s = newStmt(line, FOR);
    s->forStmt.init = init;
    s->forStmt.cond = cond;
    s->forStmt.act = act;
    s->forStmt.body = body;
    return s;
}

Stmt *newForEach(int line, Stmt *var, Expr *iter, Stmt *body) {
    Stmt *s = newStmt(line, FOREACH);
    s->forEach.var = var;
    s->forEach.iterable = iter;
    s->forEach.body = body;
    return s;
}

Stmt *newVarDecl(int line, bool isUnpack, LinkedList *ids, Expr *init) {
    Stmt *s = newStmt(line, VARDECL);
    s->varDecl.ids = ids;
    s->varDecl.isUnpack = isUnpack;
    s->varDecl.init = init;
    return s;
}

Stmt *newWhileStmt(int line, Expr *cond, Stmt *body) {
    Stmt *s = newStmt(line, WHILE);
    s->whileStmt.cond = cond;
    s->whileStmt.body = body;
    return s;
}

Stmt *newReturnStmt(int line, Expr *e) {
    Stmt *s = newStmt(line, RETURN_STMT);
    s->returnStmt.e = e;
    return s;
}

Stmt *newIfStmt(int line, Expr *cond, Stmt *thenStmt, Stmt *elseStmt) {
    Stmt *s = newStmt(line, IF);
    s->ifStmt.cond = cond;
    s->ifStmt.thenStmt = thenStmt;
    s->ifStmt.elseStmt = elseStmt;
    return s;
}

Stmt *newBlockStmt(int line, LinkedList *list) {
    Stmt *s = newStmt(line, BLOCK);
    s->blockStmt.stmts = list;
    return s;
}

Stmt *newImportStmt(int line, LinkedList *modules, LinkedList *impNames, 
    const char *as, size_t asLength) 
{
    Stmt *s = newStmt(line, IMPORT);
    s->importStmt.modules = modules;
    s->importStmt.impNames = impNames;
    s->importStmt.as.name = as;
    s->importStmt.as.length = asLength;
    return s;
}

Stmt *newExprStmt(int line, Expr *e) {
    Stmt *s = newStmt(line, EXPR);
    s->exprStmt = e;
    return s;
}

Stmt *newTryStmt(int line, Stmt *blck, LinkedList *excs, Stmt *ensure) {
    Stmt *s = newStmt(line, TRY_STMT);
    s->tryStmt.block = blck;
    s->tryStmt.excs = excs;
    s->tryStmt.ensure = ensure;
    return s;
}

Stmt *newExceptStmt(int line, Expr *cls, size_t vlen, const char *var, Stmt *block) {
    Stmt *s = newStmt(line, EXCEPT_STMT);
    s->excStmt.block = block;
    s->excStmt.cls = cls;
    s->excStmt.var.length = vlen;
    s->excStmt.var.name = var;
    return s;
}

Stmt *newRaiseStmt(int line, Expr *e) {
    Stmt *s = newStmt(line, RAISE_STMT);
    s->raiseStmt.exc = e;
    return s;
}

Stmt *newContinueStmt(int line) {
    Stmt *s = newStmt(line, CONTINUE_STMT);
    s->exprStmt = NULL;
    return s;
}

Stmt *newBreakStmt(int line) {
    Stmt *s = newStmt(line, BREAK_STMT);
    s->exprStmt = NULL;
    return s;
}

void freeStmt(Stmt *s) {
    if(s == NULL) return;

    switch(s->type) {
    case IF:
        freeExpr(s->ifStmt.cond);
        freeStmt(s->ifStmt.thenStmt);
        freeStmt(s->ifStmt.elseStmt);
        break;
    case FOR:
        freeStmt(s->forStmt.init);
        freeExpr(s->forStmt.cond);
        freeExpr(s->forStmt.act);
        freeStmt(s->forStmt.body);
        break;
    case FOREACH:
        freeStmt(s->forEach.var);
        freeExpr(s->forEach.iterable);
        freeStmt(s->forEach.body);
        break;
    case WHILE:
        freeExpr(s->whileStmt.cond);
        freeStmt(s->whileStmt.body);
        break;
    case RETURN_STMT:
        freeExpr(s->returnStmt.e);
        break;
    case EXPR:
        freeExpr(s->exprStmt);
        break;
    case BLOCK: {
        LinkedList *head = s->blockStmt.stmts;
        while(head != NULL) {
            LinkedList *f = head;
            head = head->next;
            freeStmt(f->elem);
            free(f);
        }
        break;
    }
    case FUNCDECL: {
        LinkedList *head = s->funcDecl.formalArgs;
        while(head != NULL) {
            LinkedList *f = head;
            head = head->next;
            free(f->elem);
            free(f);
        }

        head = s->funcDecl.defArgs;
        while(head != NULL) {
            LinkedList *f = head;
            head = head->next;
            freeExpr(f->elem);
            free(f);
        }

        freeStmt(s->funcDecl.body);
        break;
    }
    case NATIVEDECL: {
        LinkedList *head = s->nativeDecl.formalArgs;
        while(head != NULL) {
            LinkedList *f = head;
            head = head->next;
            free(f->elem);
            free(f);
        }

        head = s->nativeDecl.defArgs;
        while(head != NULL) {
            LinkedList *f = head;
            head = head->next;
            freeExpr(f->elem);
            free(f);
        }
        break;
    }
    case CLASSDECL: {
        freeExpr(s->classDecl.sup);
        LinkedList *head = s->classDecl.methods;
        while(head != NULL) {
            LinkedList *f = head;
            head = head->next;
            freeStmt((Stmt *)f->elem);
            free(f);
        }
        break;
    }
    case VARDECL: {
        freeExpr(s->varDecl.init);
        LinkedList *head = s->varDecl.ids;
        while(head != NULL) {
            LinkedList *f = head;
            head = head->next;
            free(f->elem);
            free(f);
        }
        break;
    }
    case TRY_STMT:
        freeStmt(s->tryStmt.block);
        freeStmt(s->tryStmt.ensure);

        LinkedList *head = s->tryStmt.excs;
        while(head != NULL) {
            LinkedList *f = head;
            head = head->next;
            freeStmt(f->elem);
            free(f);
        }
        break;
    case EXCEPT_STMT:
        freeExpr(s->excStmt.cls);
        freeStmt(s->excStmt.block);
        break;
    case RAISE_STMT:
        freeExpr(s->raiseStmt.exc);
        break;
    case WITH_STMT:
        freeExpr(s->withStmt.e);
        freeStmt(s->withStmt.block);
        break;
    case IMPORT: {
        LinkedList *head = s->importStmt.modules;
        while(head != NULL) {
            LinkedList *f = head;
            head = head->next;
            free(f->elem);
            free(f);
        }

        head = s->importStmt.impNames;
        while(head != NULL) {
            LinkedList *f = head;
            head = head->next;
            free(f->elem);
            free(f);
        }
        break;
    }
    case CONTINUE_STMT:
    case BREAK_STMT:
        break;
    }

    free(s);
}
