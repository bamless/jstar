#include "compiler.h"
#include "const.h"
#include "jstar.h"
#include "memory.h"
#include "opcode.h"
#include "util.h"
#include "value.h"
#include "vm.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

// In case of a direct assignement of the form:
//  var a, b, ..., c = x, y, ..., z
// Where the right hand side is an unpackable object (i.e. a tuple or a list)
// We can omit the creation of the tuple/list, assigning directly the elements to the variables.
// We call this type of unpack assignement a 'const unpack'
#define IS_CONST_UNPACK(type) (type == ARR_LIT || type == TUPLE_LIT)

typedef struct Local {
    Identifier id;
    bool isUpvalue;
    int depth;
} Local;

typedef struct Upvalue {
    bool isLocal;
    uint8_t index;
} Upvalue;

typedef struct Loop {
    int depth;
    size_t start;
    struct Loop *next;
} Loop;

typedef struct TryExcept {
    int depth;
    struct TryExcept *next;
} TryExcept;

typedef enum FuncType { TYPE_FUNC, TYPE_METHOD, TYPE_CTOR } FuncType;

typedef struct Compiler {
    JStarVM *vm;
    Compiler *prev;

    bool hasSuper;

    Loop *loops;

    FuncType type;
    ObjFunction *func;

    uint8_t localsCount;
    Local locals[MAX_LOCALS];

    Upvalue upvalues[MAX_LOCALS];

    bool hadError;
    int depth;

    int tryDepth;
    TryExcept *tryBlocks;
} Compiler;

static ObjFunction *function(Compiler *c, ObjModule *module, Stmt *s);
static ObjFunction *method(Compiler *c, ObjModule *module, Identifier *classId, Stmt *s);

static void initCompiler(Compiler *c, Compiler *prev, FuncType t, int depth, JStarVM *vm) {
    c->vm = vm;
    c->type = t;
    c->func = NULL;
    c->prev = prev;
    c->loops = NULL;
    c->tryDepth = 0;
    c->depth = depth;
    c->localsCount = 0;
    c->hasSuper = false;
    c->hadError = false;
    c->tryBlocks = NULL;
    vm->currCompiler = c;
}

static void endCompiler(Compiler *c) {
    if(c->prev != NULL) {
        c->prev->hadError |= c->hadError;
    }
    c->vm->currCompiler = c->prev;
}

static void error(Compiler *c, int line, const char *format, ...) {
    fprintf(stderr, "[line:%d] ", line);
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fprintf(stderr, "\n");
    c->hadError = true;
}

static size_t emitBytecode(Compiler *c, uint8_t b, int line) {
    if(line == 0 && c->func->chunk.linesCount > 0) {
        line = c->func->chunk.lines[c->func->chunk.linesCount - 1];
    }
    return writeByte(&c->func->chunk, b, line);
}

static size_t emitShort(Compiler *c, uint16_t s, int line) {
    size_t i = emitBytecode(c, (uint8_t)(s >> 8), line);
    emitBytecode(c, (uint8_t)s, line);
    return i;
}

static void discardLocal(Compiler *c, Local *local) {
    if(local->isUpvalue) {
        emitBytecode(c, OP_CLOSE_UPVALUE, 0);
    } else {
        emitBytecode(c, OP_POP, 0);
    }
}

static void enterScope(Compiler *c) {
    c->depth++;
}

static void exitScope(Compiler *c) {
    c->depth--;
    while(c->localsCount > 0 && c->locals[c->localsCount - 1].depth > c->depth) {
        discardLocal(c, &c->locals[--c->localsCount]);
    }
}

static void discardScope(Compiler *c, int depth) {
    int localsCount = c->localsCount;
    while(localsCount > 0 && c->locals[localsCount - 1].depth > depth) {
        discardLocal(c, &c->locals[--localsCount]);
    }
}

static uint16_t createConst(Compiler *c, Value constant, int line) {
    int index = addConstant(&c->func->chunk, constant);
    if(index == -1) {
        const char *name = c->func->c.name == NULL ? "<main>" : c->func->c.name->data;
        error(c, line, "too many constants in function %s", name);
        return 0;
    }
    return (uint16_t)index;
}

static Identifier syntheticIdentifier(const char *name) {
    return (Identifier){strlen(name), name};
}

static uint16_t identifierConst(Compiler *c, Identifier *id, int line) {
    ObjString *idStr = copyString(c->vm, id->name, id->length, true);
    return createConst(c, OBJ_VAL(idStr), line);
}

static void addLocal(Compiler *c, Identifier *id, int line) {
    if(c->localsCount == MAX_LOCALS) {
        error(c, line, "Too many local variables in function %s.", c->func->c.name->data);
        return;
    }
    Local *local = &c->locals[c->localsCount];
    local->isUpvalue = false;
    local->depth = -1;
    local->id = *id;
    c->localsCount++;
}

static int resolveVariable(Compiler *c, Identifier *id, bool inFunc, int line) {
    for(int i = c->localsCount - 1; i >= 0; i--) {
        if(identifierEquals(&c->locals[i].id, id)) {
            if(inFunc && c->locals[i].depth == -1) {
                error(c, line, "Cannot read local variable in its own initializer.");
                return 0;
            }
            return i;
        }
    }
    return -1;
}

static int addUpvalue(Compiler *c, uint8_t index, bool local, int line) {
    uint8_t upvalueCount = c->func->upvaluec;
    for(uint8_t i = 0; i < upvalueCount; i++) {
        Upvalue *upval = &c->upvalues[i];
        if(upval->index == index && upval->isLocal == local) {
            return i;
        }
    }

    if(c->func->upvaluec == MAX_LOCALS) {
        error(c, line, "Too many upvalues in function %s.", c->func->c.name->data);
        return -1;
    }

    c->upvalues[c->func->upvaluec].isLocal = local;
    c->upvalues[c->func->upvaluec].index = index;
    return c->func->upvaluec++;
}

static int resolveUpvalue(Compiler *c, Identifier *id, int line) {
    if(c->prev == NULL) {
        return -1;
    }

    int i = resolveVariable(c->prev, id, false, line);
    if(i != -1) {
        c->prev->locals[i].isUpvalue = true;
        return addUpvalue(c, i, true, line);
    }

    i = resolveUpvalue(c->prev, id, line);
    if(i != -1) {
        return addUpvalue(c, i, false, line);
    }

    return -1;
}

static void declareVar(Compiler *c, Identifier *id, int line) {
    if(c->depth == 0) return;

    for(int i = c->localsCount - 1; i >= 0; i--) {
        if(c->locals[i].depth != -1 && c->locals[i].depth < c->depth) break;
        if(identifierEquals(&c->locals[i].id, id)) {
            error(c, line, "Variable `%.*s` already declared.", id->length, id->name);
        }
    }

    addLocal(c, id, line);
}

static void markInitialized(Compiler *c, int id) {
    assert(id >= 0 && id < c->localsCount, "Invalid local variable");
    c->locals[id].depth = c->depth;
}

static void defineVar(Compiler *c, Identifier *id, int line) {
    if(c->depth == 0) {
        emitBytecode(c, OP_DEFINE_GLOBAL, line);
        emitShort(c, identifierConst(c, id, line), line);
    } else {
        markInitialized(c, c->localsCount - 1);
    }
}

static size_t emitJumpTo(Compiler *c, int jmpOpcode, size_t target, int line) {
    int32_t offset = target - (c->func->chunk.count + 3);
    if(offset > INT16_MAX || offset < INT16_MIN) {
        error(c, line, "Too much code to jump over.");
    }
    emitBytecode(c, jmpOpcode, 0);
    emitShort(c, (uint16_t)offset, 0);
    return c->func->chunk.count - 2;
}

static void setJumpTo(Compiler *c, size_t jumpAddr, size_t target, int line) {
    int32_t offset = target - (jumpAddr + 3);
    if(offset > INT16_MAX || offset < INT16_MIN) {
        error(c, line, "Too much code to jump over.");
    }
    Chunk *chunk = &c->func->chunk;
    chunk->code[jumpAddr + 1] = (uint8_t)((uint16_t)offset >> 8);
    chunk->code[jumpAddr + 2] = (uint8_t)((uint16_t)offset);
}

static void startLoop(Compiler *c, Loop *loop) {
    loop->depth = c->depth;
    loop->start = c->func->chunk.count;
    loop->next = c->loops;
    c->loops = loop;
}

static void patchLoopExitStmts(Compiler *c, size_t start, size_t cont, size_t brk) {
    for(size_t i = start; i < c->func->chunk.count; i++) {
        Opcode code = c->func->chunk.code[i];
        if(code == OP_SIGN_BRK || code == OP_SIGN_CONT) {
            c->func->chunk.code[i] = OP_JUMP;
            setJumpTo(c, i, code == OP_SIGN_CONT ? cont : brk, 0);
            code = OP_JUMP;
        }
        i += opcodeArgsNumber(code);
    }
}

static void endLoop(Compiler *c) {
    patchLoopExitStmts(c, c->loops->start, c->loops->start, c->func->chunk.count);
    c->loops = c->loops->next;
}

static void callMethod(Compiler *c, const char *name, int args) {
    Identifier meth = syntheticIdentifier(name);
    emitBytecode(c, OP_INVOKE_0 + args, 0);
    emitShort(c, identifierConst(c, &meth, 0), 0);
}

static ObjString *readString(Compiler *c, Expr *e);

static void addDefaultConsts(Compiler *c, Value *defaults, LinkedList *defArgs) {
    int i = 0;
    foreach(n, defArgs) {
        Expr *e = (Expr *)n->elem;
        switch(e->type) {
        case NUM_LIT:
            defaults[i++] = NUM_VAL(e->num);
            break;
        case BOOL_LIT:
            defaults[i++] = BOOL_VAL(e->boolean);
            break;
        case STR_LIT:
            defaults[i++] = OBJ_VAL(readString(c, e));
            break;
        case NULL_LIT:
            defaults[i++] = NULL_VAL;
            break;
        default:
            break;
        }
    }
}

static void compileExpr(Compiler *c, Expr *e);

static void compileBinaryExpr(Compiler *c, Expr *e) {
    compileExpr(c, e->bin.left);
    compileExpr(c, e->bin.right);
    switch(e->bin.op) {
    case PLUS:
        emitBytecode(c, OP_ADD, e->line);
        break;
    case MINUS:
        emitBytecode(c, OP_SUB, e->line);
        break;
    case MULT:
        emitBytecode(c, OP_MUL, e->line);
        break;
    case DIV:
        emitBytecode(c, OP_DIV, e->line);
        break;
    case MOD:
        emitBytecode(c, OP_MOD, e->line);
        break;
    case EQ:
        emitBytecode(c, OP_EQ, e->line);
        break;
    case GT:
        emitBytecode(c, OP_GT, e->line);
        break;
    case GE:
        emitBytecode(c, OP_GE, e->line);
        break;
    case LT:
        emitBytecode(c, OP_LT, e->line);
        break;
    case LE:
        emitBytecode(c, OP_LE, e->line);
        break;
    case IS:
        emitBytecode(c, OP_IS, e->line);
        break;
    case NEQ:
        emitBytecode(c, OP_EQ, e->line);
        emitBytecode(c, OP_NOT, e->line);
        break;
    default:
        UNREACHABLE();
        break;
    }
}

static void compileLogicExpr(Compiler *c, Expr *e) {
    compileExpr(c, e->bin.left);
    emitBytecode(c, OP_DUP, e->line);

    uint8_t jmp = e->bin.op == AND ? OP_JUMPF : OP_JUMPT;
    size_t scJmp = emitBytecode(c, jmp, 0);
    emitShort(c, 0, 0);

    emitBytecode(c, OP_POP, e->line);
    compileExpr(c, e->bin.right);

    setJumpTo(c, scJmp, c->func->chunk.count, e->line);
}

static void compileUnaryExpr(Compiler *c, Expr *e) {
    compileExpr(c, e->unary.operand);
    switch(e->unary.op) {
    case MINUS:
        emitBytecode(c, OP_NEG, e->line);
        break;
    case NOT:
        emitBytecode(c, OP_NOT, e->line);
        break;
    case LENGTH:
        callMethod(c, "__len__", 0);
        break;
    case STRINGOP:
        callMethod(c, "__string__", 0);
        break;
    default:
        UNREACHABLE();
        break;
    }
}

static void compileTernaryExpr(Compiler *c, Expr *e) {
    compileExpr(c, e->ternary.cond);

    size_t falseJmp = emitBytecode(c, OP_JUMPF, e->line);
    emitShort(c, 0, 0);

    compileExpr(c, e->ternary.thenExpr);
    size_t exitJmp = emitBytecode(c, OP_JUMP, e->line);
    emitShort(c, 0, 0);

    setJumpTo(c, falseJmp, c->func->chunk.count, e->line);
    compileExpr(c, e->ternary.elseExpr);

    setJumpTo(c, exitJmp, c->func->chunk.count, e->line);
}

static void compileVariable(Compiler *c, Identifier *id, bool set, int line) {
    int i = resolveVariable(c, id, true, line);
    if(i != -1) {
        if(set)
            emitBytecode(c, OP_SET_LOCAL, line);
        else
            emitBytecode(c, OP_GET_LOCAL, line);
        emitBytecode(c, i, line);
    } else if((i = resolveUpvalue(c, id, line)) != -1) {
        if(set)
            emitBytecode(c, OP_SET_UPVALUE, line);
        else
            emitBytecode(c, OP_GET_UPVALUE, line);
        emitBytecode(c, i, line);
    } else {
        if(set)
            emitBytecode(c, OP_SET_GLOBAL, line);
        else
            emitBytecode(c, OP_GET_GLOBAL, line);
        emitShort(c, identifierConst(c, id, line), line);
    }
}

static void compileFunction(Compiler *c, Stmt *s);

static void compileAnonymousFunc(Compiler *c, Identifier *name, Expr *e) {
    Stmt *f = e->anonFunc.func;
    if(name != NULL) {
        f->funcDecl.id.length = name->length;
        f->funcDecl.id.name = name->name;
        compileFunction(c, f);
    } else {
        char funcName[5 + MAX_STRLEN_FOR_INT_TYPE(int) + 1];
        sprintf(funcName, "anon:%d", f->line);

        f->funcDecl.id.length = strlen(funcName);
        f->funcDecl.id.name = funcName;

        compileFunction(c, f);
    }
}

static void compileLval(Compiler *c, Expr *e) {
    switch(e->type) {
    case VAR_LIT:
        compileVariable(c, &e->var.id, true, e->line);
        break;
    case ACCESS_EXPR: {
        compileExpr(c, e->accessExpr.left);
        emitBytecode(c, OP_SET_FIELD, e->line);
        emitShort(c, identifierConst(c, &e->accessExpr.id, e->line), e->line);
        break;
    }
    case ARR_ACC: {
        compileExpr(c, e->arrAccExpr.left);
        compileExpr(c, e->arrAccExpr.index);
        emitBytecode(c, OP_SUBSCR_SET, e->line);
        break;
    }
    default:
        UNREACHABLE();
        break;
    }
}

static void compileRval(Compiler *c, Identifier *boundName, Expr *e) {
    if(e->type != ANON_FUNC)
        compileExpr(c, e);
    else
        compileAnonymousFunc(c, boundName, e);
}

static void compileConstUnpackLst(Compiler *c, Identifier **boundNames, Expr *exprs, int num) {
    int i = 0;
    foreach(n, exprs->exprList.lst) {
        compileRval(c, boundNames ? boundNames[i] : NULL, (Expr *)n->elem);
        if(++i > num) emitBytecode(c, OP_POP, 0);
    }
    if(i < num) {
        error(c, exprs->line, "Too little values to unpack.");
    }
}

static void compileAssignExpr(Compiler *c, Expr *e) {
    switch(e->assign.lval->type) {
    case VAR_LIT: {
        Identifier *name = &e->assign.lval->var.id;
        compileRval(c, name, e->assign.rval);
        compileLval(c, e->assign.lval);
        break;
    }
    case ACCESS_EXPR: {
        Identifier *name = &e->assign.lval->accessExpr.id;
        compileRval(c, name, e->assign.rval);
        compileLval(c, e->assign.lval);
        break;
    }
    case ARR_ACC: {
        compileRval(c, NULL, e->assign.rval);
        compileLval(c, e->assign.lval);
        break;
    }

    // Unpack assignement of the form: a, b, ..., c = ...
    case TUPLE_LIT: {
        int assignments = 0;
        Expr *lvals[UINT8_MAX];

        foreach(n, e->assign.lval->tuple.exprs->exprList.lst) {
            if(assignments == UINT8_MAX) {
                error(c, e->line, "Exceeded max number of unpack assignment (%d).", UINT8_MAX);
                break;
            }
            lvals[assignments++] = (Expr *)n->elem;
        }

        Expr *rval = e->assign.rval;

        if(IS_CONST_UNPACK(rval->type)) {
            Expr *lst = rval->type == ARR_LIT ? rval->arr.exprs : rval->tuple.exprs;
            compileConstUnpackLst(c, NULL, lst, assignments);
        } else {
            compileRval(c, NULL, e->assign.rval);
            emitBytecode(c, OP_UNPACK, e->line);
            emitBytecode(c, (uint8_t)assignments, e->line);
        }

        // compile lvals in reverse order in order to assign correct values to variables in case
        // of a const unpack
        for(int n = assignments - 1; n >= 0; n--) {
            compileLval(c, lvals[n]);
            if(n != 0) emitBytecode(c, OP_POP, e->line);
        }
        break;
    }
    default:
        UNREACHABLE();
        break;
    }
}

static void compileCompundAssign(Compiler *c, Expr *e) {
    Operator op = e->compundAssign.op;
    Expr *l = e->compundAssign.lval;
    Expr *r = e->compundAssign.rval;

    // expand compound assignement (e.g. a op= b -> a = a op b)
    Expr binary = {e->line, BINARY, .bin = {op, l, r}};
    Expr assignment = {e->line, ASSIGN, .assign = {l, &binary}};

    // compile as a normal assignment
    compileAssignExpr(c, &assignment);
}

static void compileCallExpr(Compiler *c, Expr *e) {
    Opcode callCode = OP_CALL;
    Opcode callInline = OP_CALL_0;

    Expr *callee = e->callExpr.callee;
    bool isMethod = callee->type == ACCESS_EXPR;

    if(isMethod) {
        bool isSuper = callee->accessExpr.left->type == SUPER_LIT;
        int line = callee->accessExpr.left->line;

        if(isSuper && c->type != TYPE_METHOD && c->type != TYPE_CTOR) {
            error(c, line, "Can't use `super` outside method.");
        }

        callCode = isSuper ? OP_SUPER : OP_INVOKE;
        callInline = isSuper ? OP_SUPER_0 : OP_INVOKE_0;
        if(isSuper) {
            emitBytecode(c, OP_GET_LOCAL, e->line);
            emitBytecode(c, 0, e->line);
        } else {
            compileExpr(c, callee->accessExpr.left);
        }
    } else {
        compileExpr(c, callee);
    }

    uint8_t argsc = 0;
    foreach(n, e->callExpr.args->exprList.lst) {
        if(argsc++ == UINT8_MAX) {
            error(c, e->line, "Too many arguments for function %s.", c->func->c.name->data);
            return;
        }
        compileExpr(c, (Expr *)n->elem);
    }

    if(argsc <= 10) {
        emitBytecode(c, callInline + argsc, e->line);
    } else {
        emitBytecode(c, callCode, e->line);
        emitBytecode(c, argsc, e->line);
    }

    if(isMethod) {
        emitShort(c, identifierConst(c, &callee->accessExpr.id, e->line), e->line);
    }
}

static void compileAccessExpression(Compiler *c, Expr *e) {
    compileExpr(c, e->accessExpr.left);
    emitBytecode(c, OP_GET_FIELD, e->line);
    emitShort(c, identifierConst(c, &e->accessExpr.id, e->line), e->line);
}

static void compileArraryAccExpression(Compiler *c, Expr *e) {
    compileExpr(c, e->arrAccExpr.left);
    compileExpr(c, e->arrAccExpr.index);
    emitBytecode(c, OP_SUBSCR_GET, e->line);
}

static void compileExpExpr(Compiler *c, Expr *e) {
    compileExpr(c, e->expExpr.base);
    compileExpr(c, e->expExpr.exp);
    emitBytecode(c, OP_POW, e->line);
}

static ObjString *readString(Compiler *c, Expr *e);

static void compileExpr(Compiler *c, Expr *e) {
    switch(e->type) {
    case ASSIGN:
        compileAssignExpr(c, e);
        break;
    case COMP_ASSIGN:
        compileCompundAssign(c, e);
        break;
    case BINARY:
        if(e->bin.op == AND || e->bin.op == OR)
            compileLogicExpr(c, e);
        else
            compileBinaryExpr(c, e);
        break;
    case UNARY:
        compileUnaryExpr(c, e);
        break;
    case TERNARY:
        compileTernaryExpr(c, e);
        break;
    case CALL_EXPR:
        compileCallExpr(c, e);
        break;
    case ACCESS_EXPR:
        compileAccessExpression(c, e);
        break;
    case ARR_ACC:
        compileArraryAccExpression(c, e);
        break;
    case EXP_EXPR:
        compileExpExpr(c, e);
        break;
    case EXPR_LST: {
        foreach(n, e->exprList.lst) { 
            compileExpr(c, (Expr *)n->elem); 
        }
        break;
    }
    case NUM_LIT:
        emitBytecode(c, OP_GET_CONST, e->line);
        emitShort(c, createConst(c, NUM_VAL(e->num), e->line), e->line);
        break;
    case BOOL_LIT:
        emitBytecode(c, OP_GET_CONST, e->line);
        emitShort(c, createConst(c, BOOL_VAL(e->boolean), e->line), e->line);
        break;
    case STR_LIT: {
        ObjString *str = readString(c, e);
        emitBytecode(c, OP_GET_CONST, e->line);
        emitShort(c, createConst(c, OBJ_VAL(str), e->line), e->line);
        break;
    }
    case VAR_LIT:
        compileVariable(c, &e->var.id, false, e->line);
        break;
    case NULL_LIT:
        emitBytecode(c, OP_NULL, e->line);
        break;
    case ARR_LIT: {
        emitBytecode(c, OP_NEW_LIST, e->line);
        LinkedList *exprs = e->arr.exprs->exprList.lst;
        foreach(n, exprs) {
            compileExpr(c, (Expr *)n->elem);
            emitBytecode(c, OP_APPEND_LIST, e->line);
        }
        break;
    }
    case TUPLE_LIT: {
        LinkedList *exprs = e->tuple.exprs->exprList.lst;

        int i = 0;
        foreach(n, exprs) {
            compileExpr(c, (Expr *)n->elem);
            i++;
        }
        if(i > UINT8_MAX) {
            error(c, e->line, "Too many elements in tuple literal.");
            break;
        }
        emitBytecode(c, OP_NEW_TUPLE, e->line);
        emitBytecode(c, i, e->line);
        break;
    }
    case TABLE_LIT: {
        emitBytecode(c, OP_NEW_TABLE, e->line);

        LinkedList *head = e->table.keyVals->exprList.lst;
        while(head) {
            Expr *key = (Expr *)head->elem;
            Expr *val = (Expr *)head->next->elem;

            emitBytecode(c, OP_DUP, e->line);
            compileExpr(c, key);
            compileExpr(c, val);
            callMethod(c, "__set__", 2);
            emitBytecode(c, OP_POP, e->line);

            head = head->next->next;
        }
        break;
    }
    case ANON_FUNC:
        compileAnonymousFunc(c, NULL, e);
        break;
    case SUPER_LIT:
        error(c, e->line, "Can only use `super` in method call");
        break;
    }
}

static void compileVarDecl(Compiler *c, Stmt *s) {
    int numDecls = 0;
    Identifier *decls[MAX_LOCALS];

    foreach(n, s->varDecl.ids) {
        if(numDecls == MAX_LOCALS) break;
        Identifier *name = (Identifier *)n->elem;
        declareVar(c, name, s->line);
        decls[numDecls++] = name;
    }

    if(s->varDecl.init != NULL) {
        ExprType initType = s->varDecl.init->type;

        if(s->varDecl.isUnpack && IS_CONST_UNPACK(initType)) {
            Expr *e = initType == ARR_LIT ? s->varDecl.init->arr.exprs : s->varDecl.init->tuple.exprs;
            compileConstUnpackLst(c, decls, e, numDecls);
        } else {
            compileRval(c, decls[0], s->varDecl.init);
            if(s->varDecl.isUnpack) {
                emitBytecode(c, OP_UNPACK, s->line);
                emitBytecode(c, (uint8_t)numDecls, s->line);
            }
        }
    } else {
        for(int i = 0; i < numDecls; i++) {
            emitBytecode(c, OP_NULL, s->line);
        }
    }

    // define in reverse order in order to assign correct values to variables in case of a
    // const unpack
    for(int i = numDecls - 1; i >= 0; i--) {
        if(c->depth == 0)
            defineVar(c, decls[i], s->line);
        else
            markInitialized(c, c->localsCount - i - 1);
    }
}

static void compileStatement(Compiler *c, Stmt *s);
static void compileStatements(Compiler *c, LinkedList *stmts);

static void compileReturn(Compiler *c, Stmt *s) {
    if(c->prev == NULL) {
        error(c, s->line, "Cannot use return in global scope.");
    }
    if(c->type == TYPE_CTOR) {
        error(c, s->line, "Cannot use return in constructor.");
    }

    if(s->returnStmt.e != NULL) {
        compileExpr(c, s->returnStmt.e);
    } else {
        emitBytecode(c, OP_NULL, s->line);
    }

    emitBytecode(c, OP_RETURN, s->line);
}

static void compileIfStatement(Compiler *c, Stmt *s) {
    // compile the condition
    compileExpr(c, s->ifStmt.cond);

    // emit the jump istr for false condtion with dummy address
    size_t falseJmp = emitBytecode(c, OP_JUMPF, 0);
    emitShort(c, 0, 0);

    // compile 'then' branch
    compileStatement(c, s->ifStmt.thenStmt);

    // if the 'if' has an 'else' emit istruction to jump over the 'else' branch
    size_t exitJmp = 0;
    if(s->ifStmt.elseStmt != NULL) {
        exitJmp = emitBytecode(c, OP_JUMP, 0);
        emitShort(c, 0, 0);
    }

    // set the false jump to the 'else' branch (or to exit if not present)
    setJumpTo(c, falseJmp, c->func->chunk.count, s->line);

    // If present compile 'else' branch and set the exit jump to 'else' end
    if(s->ifStmt.elseStmt != NULL) {
        compileStatement(c, s->ifStmt.elseStmt);
        setJumpTo(c, exitJmp, c->func->chunk.count, s->line);
    }
}

static void compileForStatement(Compiler *c, Stmt *s) {
    enterScope(c);

    // init
    if(s->forStmt.init != NULL) {
        compileStatement(c, s->forStmt.init);
    }

    size_t firstJmp = 0;
    if(s->forStmt.act != NULL) {
        firstJmp = emitBytecode(c, OP_JUMP, s->line);
        emitShort(c, 0, 0);
    }

    Loop l;
    startLoop(c, &l);

    // act
    if(s->forStmt.act != NULL) {
        compileExpr(c, s->forStmt.act);
        emitBytecode(c, OP_POP, 0);
        setJumpTo(c, firstJmp, c->func->chunk.count, s->line);
    }

    // condition
    size_t exitJmp = 0;
    if(s->forStmt.cond != NULL) {
        compileExpr(c, s->forStmt.cond);
        exitJmp = emitBytecode(c, OP_JUMPF, 0);
        emitShort(c, 0, 0);
    }

    // body
    compileStatement(c, s->forStmt.body);

    // jump back to for start
    emitJumpTo(c, OP_JUMP, l.start, 0);

    // set the exit jump
    if(s->forStmt.cond != NULL) {
        setJumpTo(c, exitJmp, c->func->chunk.count, s->line);
    }

    endLoop(c);
    exitScope(c);
}

/*
 * for var i in iterable do
 *     ...
 * end
 *
 * begin
 *     var _expr = iterable
 *     var _iter
 *     while _iter = _expr.__iter__(_iter) do
 *         var i = _expr.__next__(_iter)
 *         ...
 *     end
 * end
 **/
static void compileForEach(Compiler *c, Stmt *s) {
    enterScope(c);

    Identifier expr = syntheticIdentifier(".expr");
    declareVar(c, &expr, s->forEach.iterable->line);
    defineVar(c, &expr, s->forEach.iterable->line);
    int exprID = c->localsCount - 1;

    compileExpr(c, s->forEach.iterable);

    // set the iterator variable with a name that it's not an identifier.
    // this will avoid the user shadowing the iterator with a declared variable.
    Identifier iterator = syntheticIdentifier(".iter");
    declareVar(c, &iterator, s->line);
    defineVar(c, &iterator, s->line);

    int iterID = c->localsCount - 1;

    emitBytecode(c, OP_NULL, 0);

    Loop l;
    startLoop(c, &l);

    emitBytecode(c, OP_GET_LOCAL, 0);
    emitBytecode(c, exprID, 0);
    emitBytecode(c, OP_GET_LOCAL, 0);
    emitBytecode(c, iterID, 0);
    callMethod(c, "__iter__", 1);

    emitBytecode(c, OP_SET_LOCAL, 0);
    emitBytecode(c, iterID, 0);

    size_t exitJmp = emitBytecode(c, OP_JUMPF, 0);
    emitShort(c, 0, 0);

    emitBytecode(c, OP_GET_LOCAL, 0);
    emitBytecode(c, exprID, 0);
    emitBytecode(c, OP_GET_LOCAL, 0);
    emitBytecode(c, iterID, 0);
    callMethod(c, "__next__", 1);

    Stmt *varDecl = s->forEach.var;

    enterScope(c);

    // declare the variables used for iteration
    int num = 0;

    foreach(n, varDecl->varDecl.ids) {
        declareVar(c, (Identifier *)n->elem, s->line);
        defineVar(c, (Identifier *)n->elem, s->line);
        num++;
    }

    if(varDecl->varDecl.isUnpack) {
        emitBytecode(c, OP_UNPACK, s->line);
        emitBytecode(c, (uint8_t)num, s->line);
    }

    compileStatements(c, s->forEach.body->blockStmt.stmts);

    exitScope(c);

    emitJumpTo(c, OP_JUMP, l.start, 0);
    setJumpTo(c, exitJmp, c->func->chunk.count, s->line);

    endLoop(c);
    exitScope(c);
}

static void compileWhileStatement(Compiler *c, Stmt *s) {
    Loop l;
    startLoop(c, &l);

    compileExpr(c, s->whileStmt.cond);
    size_t exitJmp = emitBytecode(c, OP_JUMPF, 0);
    emitShort(c, 0, 0);

    compileStatement(c, s->whileStmt.body);

    emitJumpTo(c, OP_JUMP, l.start, 0);
    setJumpTo(c, exitJmp, c->func->chunk.count, s->line);

    endLoop(c);
}

static void compileFunction(Compiler *c, Stmt *s) {
    Compiler compiler;
    initCompiler(&compiler, c, TYPE_FUNC, c->depth + 1, c->vm);

    ObjFunction *func = function(&compiler, c->func->c.module, s);

    emitBytecode(c, OP_CLOSURE, s->line);
    emitShort(c, createConst(c, OBJ_VAL(func), s->line), s->line);

    for(uint8_t i = 0; i < func->upvaluec; i++) {
        emitBytecode(c, compiler.upvalues[i].isLocal ? 1 : 0, s->line);
        emitBytecode(c, compiler.upvalues[i].index, s->line);
    }

    endCompiler(&compiler);
}

static void compileNative(Compiler *c, Stmt *s) {
    size_t defaults = listLength(s->nativeDecl.defArgs);
    size_t arity = listLength(s->nativeDecl.formalArgs);

    ObjNative *native = newNative(c->vm, c->func->c.module, NULL, arity, NULL, defaults);
    native->c.vararg = s->nativeDecl.isVararg;
    addDefaultConsts(c, native->c.defaults, s->nativeDecl.defArgs);

    uint16_t nativeConst = createConst(c, OBJ_VAL(native), s->line);
    uint16_t nameConst = identifierConst(c, &s->nativeDecl.id, s->line);
    native->c.name = AS_STRING(c->func->chunk.consts.arr[nameConst]);

    emitBytecode(c, OP_GET_CONST, s->line);
    emitShort(c, nativeConst, s->line);

    emitBytecode(c, OP_NATIVE, s->line);
    emitShort(c, nameConst, s->line);
}

static void compileMethods(Compiler *c, Stmt *cls) {
    LinkedList *methods = cls->classDecl.methods;

    Compiler methCompiler;
    foreach(n, methods) {
        Stmt *m = (Stmt *)n->elem;
        switch(m->type) {
        case FUNCDECL: {
            initCompiler(&methCompiler, c, TYPE_METHOD, c->depth + 1, c->vm);

            ObjFunction *meth = method(&methCompiler, c->func->c.module, &cls->classDecl.id, m);

            emitBytecode(c, OP_CLOSURE, m->line);
            emitShort(c, createConst(c, OBJ_VAL(meth), m->line), m->line);

            for(uint8_t i = 0; i < meth->upvaluec; i++) {
                emitBytecode(c, methCompiler.upvalues[i].isLocal ? 1 : 0, m->line);
                emitBytecode(c, methCompiler.upvalues[i].index, m->line);
            }

            emitBytecode(c, OP_DEF_METHOD, cls->line);
            emitShort(c, identifierConst(c, &m->funcDecl.id, m->line), cls->line);

            endCompiler(&methCompiler);
            break;
        }
        case NATIVEDECL: {
            size_t defaults = listLength(m->nativeDecl.defArgs);
            size_t arity = listLength(m->nativeDecl.formalArgs);

            ObjNative *n = newNative(c->vm, c->func->c.module, NULL, arity, NULL, defaults);
            n->c.vararg = m->nativeDecl.isVararg;
            addDefaultConsts(c, n->c.defaults, m->nativeDecl.defArgs);

            uint16_t native = createConst(c, OBJ_VAL(n), cls->line);
            uint16_t id = identifierConst(c, &m->nativeDecl.id, m->line);

            Identifier *classId = &cls->classDecl.id;
            size_t len = classId->length + m->nativeDecl.id.length + 1;
            ObjString *name = allocateString(c->vm, len);

            memcpy(name->data, classId->name, classId->length);
            name->data[classId->length] = '.';
            memcpy(name->data + classId->length + 1, m->nativeDecl.id.name, m->nativeDecl.id.length);

            n->c.name = name;

            emitBytecode(c, OP_NAT_METHOD, cls->line);
            emitShort(c, id, cls->line);
            emitShort(c, native, cls->line);
            break;
        }
        default:
            break;
        }
    }
}

static void compileClass(Compiler *c, Stmt *s) {
    declareVar(c, &s->classDecl.id, s->line);

    bool isSubClass = s->classDecl.sup != NULL;
    if(isSubClass) {
        compileExpr(c, s->classDecl.sup);
        emitBytecode(c, OP_NEW_SUBCLASS, s->line);
    } else {
        emitBytecode(c, OP_NEW_CLASS, s->line);
    }

    emitShort(c, identifierConst(c, &s->classDecl.id, s->line), s->line);
    compileMethods(c, s);

    defineVar(c, &s->classDecl.id, s->line);
}

static void compileImportStatement(Compiler *c, Stmt *s) {
    const char *base = ((Identifier *)s->importStmt.modules->elem)->name;

    // import module (if nested module, import all from outer to inner)
    uint16_t nameConst;
    int length = -1;
    foreach(n, s->importStmt.modules) {
        Identifier *name = (Identifier *)n->elem;

        // create fully qualified name of module
        length += name->length + 1;         // length of current submodule plus a dot
        Identifier module = {length, base}; // name of current submodule

        if(n == s->importStmt.modules && s->importStmt.impNames == NULL &&
           s->importStmt.as.name == NULL) {
            emitBytecode(c, OP_IMPORT, s->line);
        } else {
            emitBytecode(c, OP_IMPORT_FROM, s->line);
        }
        nameConst = identifierConst(c, &module, s->line);
        emitShort(c, nameConst, s->line);

        if(n->next != NULL) {
            emitBytecode(c, OP_POP, s->line);
        }
    }

    if(s->importStmt.impNames != NULL) {
        foreach(n, s->importStmt.impNames) {
            emitBytecode(c, OP_IMPORT_NAME, s->line);
            emitShort(c, nameConst, s->line);
            emitShort(c, identifierConst(c, (Identifier *)n->elem, s->line), s->line);
        }
    } else if(s->importStmt.as.name != NULL) {
        // set last import as an import as
        c->func->chunk.code[c->func->chunk.count - 3] = OP_IMPORT_AS;
        // emit the as name
        emitShort(c, identifierConst(c, &s->importStmt.as, s->line), s->line);
    }

    emitBytecode(c, OP_POP, s->line);
}

static void compileExcepts(Compiler *c, LinkedList *excs) {
    Stmt *exc = (Stmt *)excs->elem;

    Identifier excId = syntheticIdentifier(".exception");
    uint8_t exception = resolveVariable(c, &excId, true, exc->line);

    emitBytecode(c, OP_GET_LOCAL, exc->line);
    emitBytecode(c, exception, exc->line);
    compileExpr(c, exc->excStmt.cls);
    emitBytecode(c, OP_IS, 0);

    size_t falseJmp = emitBytecode(c, OP_JUMPF, 0);
    emitShort(c, 0, 0);

    enterScope(c);

    emitBytecode(c, OP_GET_LOCAL, exc->line);
    emitBytecode(c, exception, exc->line);
    declareVar(c, &exc->excStmt.var, exc->line);
    defineVar(c, &exc->excStmt.var, exc->line);

    compileStatements(c, exc->excStmt.block->blockStmt.stmts);

    emitBytecode(c, OP_NULL, exc->line);
    emitBytecode(c, OP_SET_LOCAL, exc->line);

    emitBytecode(c, exception, exc->line);
    emitBytecode(c, OP_POP, exc->line);

    exitScope(c);

    size_t exitJmp = 0;
    if(excs->next != NULL) {
        exitJmp = emitBytecode(c, OP_JUMP, 0);
        emitShort(c, 0, 0);
    }

    setJumpTo(c, falseJmp, c->func->chunk.count, exc->line);

    if(excs->next != NULL) {
        compileExcepts(c, excs->next);
        setJumpTo(c, exitJmp, c->func->chunk.count, exc->line);
    }
}

static void enterTryBlock(Compiler *c, TryExcept *tryExc, Stmt *try) {
    tryExc->depth = c->depth;
    tryExc->next = c->tryBlocks;
    c->tryBlocks = tryExc;
    if(try->tryStmt.ensure != NULL) c->tryDepth++;
    if(try->tryStmt.excs != NULL) c->tryDepth++;
}

static void exitTryBlock(Compiler *c, Stmt *try) {
    c->tryBlocks = c->tryBlocks->next;
    if(try->tryStmt.ensure != NULL) c->tryDepth--;
    if(try->tryStmt.excs != NULL) c->tryDepth--;
}

static void compileTryExcept(Compiler *c, Stmt *s) {
    TryExcept tryBlock;
    enterTryBlock(c, &tryBlock, s);

    if(c->tryDepth > MAX_TRY_DEPTH) {
        error(c, s->line, "Exceeded max number of nested try blocks (%d)", MAX_TRY_DEPTH);
    }

    bool hasExcept = s->tryStmt.excs != NULL;
    bool hasEnsure = s->tryStmt.ensure != NULL;

    size_t excSetup = 0;
    size_t ensSetup = 0;

    if(hasEnsure) {
        ensSetup = emitBytecode(c, OP_SETUP_ENSURE, s->line);
        emitShort(c, 0, 0);
    }
    if(hasExcept) {
        excSetup = emitBytecode(c, OP_SETUP_EXCEPT, s->line);
        emitShort(c, 0, 0);
    }

    compileStatement(c, s->tryStmt.block);

    if(hasExcept) emitBytecode(c, OP_POP_HANDLER, s->line);

    if(hasEnsure) {
        emitBytecode(c, OP_POP_HANDLER, s->line);
        // esnure block expects exception on top or the
        // stack or null if no exception has been raised
        emitBytecode(c, OP_NULL, s->line);
        // the cause of the unwind null for none, CAUSE_RETURN or CAUSE_EXCEPT
        emitBytecode(c, OP_NULL, s->line);
    }

    enterScope(c);

    Identifier exc = syntheticIdentifier(".exception");
    declareVar(c, &exc, 0);
    defineVar(c, &exc, 0);

    Identifier cause = syntheticIdentifier(".cause");
    declareVar(c, &cause, 0);
    defineVar(c, &cause, 0);

    if(hasExcept) {
        size_t excJmp = emitBytecode(c, OP_JUMP, 0);
        emitShort(c, 0, 0);

        setJumpTo(c, excSetup, c->func->chunk.count, s->line);

        compileExcepts(c, s->tryStmt.excs);

        if(hasEnsure) {
            emitBytecode(c, OP_POP_HANDLER, 0);
        } else {
            emitBytecode(c, OP_ENSURE_END, 0);
            exitScope(c);
        }

        setJumpTo(c, excJmp, c->func->chunk.count, 0);
    }

    if(hasEnsure) {
        setJumpTo(c, ensSetup, c->func->chunk.count, s->line);
        compileStatements(c, s->tryStmt.ensure->blockStmt.stmts);
        emitBytecode(c, OP_ENSURE_END, 0);
        exitScope(c);
    }

    exitTryBlock(c, s);
}

static void compileRaiseStmt(Compiler *c, Stmt *s) {
    compileExpr(c, s->raiseStmt.exc);
    emitBytecode(c, OP_RAISE, s->line);
}

static void compileLoopExitStmt(Compiler *c, Stmt *s) {
    bool isBreak = s->type == BREAK_STMT;

    if(c->loops == NULL) {
        error(c, s->line, "cannot use %s outside loop.", isBreak ? "break" : "continue");
        return;
    }
    if(c->tryDepth != 0 && c->tryBlocks->depth >= c->loops->depth) {
        error(c, s->line, "cannot use %s across a try except.", isBreak ? "break" : "continue");
    }

    discardScope(c, c->loops->depth);
    emitBytecode(c, isBreak ? OP_SIGN_BRK : OP_SIGN_CONT, s->line);
    emitShort(c, 0, 0);
}

static void compileStatement(Compiler *c, Stmt *s) {
    switch(s->type) {
    case IF:
        compileIfStatement(c, s);
        break;
    case FOR:
        compileForStatement(c, s);
        break;
    case FOREACH:
        compileForEach(c, s);
        break;
    case WHILE:
        compileWhileStatement(c, s);
        break;
    case BLOCK:
        enterScope(c);
        compileStatements(c, s->blockStmt.stmts);
        exitScope(c);
        break;
    case RETURN_STMT:
        compileReturn(c, s);
        break;
    case EXPR:
        compileExpr(c, s->exprStmt);
        emitBytecode(c, OP_POP, 0);
        break;
    case VARDECL:
        compileVarDecl(c, s);
        break;
    case FUNCDECL:
        declareVar(c, &s->funcDecl.id, s->line);
        compileFunction(c, s);
        defineVar(c, &s->funcDecl.id, s->line);
        break;
    case NATIVEDECL:
        declareVar(c, &s->funcDecl.id, s->line);
        compileNative(c, s);
        defineVar(c, &s->funcDecl.id, s->line);
        break;
    case CLASSDECL:
        compileClass(c, s);
        break;
    case IMPORT:
        compileImportStatement(c, s);
        break;
    case RAISE_STMT:
        compileRaiseStmt(c, s);
        break;
    case TRY_STMT:
        compileTryExcept(c, s);
        break;
    case CONTINUE_STMT:
    case BREAK_STMT:
        compileLoopExitStmt(c, s);
        break;
    case EXCEPT_STMT:
        UNREACHABLE();
        break;
    }
}

static void compileStatements(Compiler *c, LinkedList *stmts) {
    foreach(n, stmts) { 
        compileStatement(c, (Stmt *)n->elem); 
    }
}

ObjFunction *compile(JStarVM *vm, ObjModule *module, Stmt *s) {
    Compiler c;
    initCompiler(&c, NULL, TYPE_FUNC, -1, vm);
    ObjFunction *func = function(&c, module, s);
    endCompiler(&c);
    return c.hadError ? NULL : func;
}

static void enterFunctionScope(Compiler *c) {
    c->depth++;
}

static void exitFunctionScope(Compiler *c) {
    c->depth--;
}

static ObjFunction *function(Compiler *c, ObjModule *module, Stmt *s) {
    size_t defaults = listLength(s->funcDecl.defArgs);
    size_t arity = listLength(s->funcDecl.formalArgs);

    c->func = newFunction(c->vm, module, NULL, arity, defaults);
    c->func->c.vararg = s->funcDecl.isVararg;

    // Add default parameters
    addDefaultConsts(c, c->func->c.defaults, s->funcDecl.defArgs);

    if(s->funcDecl.id.length != 0) {
        c->func->c.name = copyString(c->vm, s->funcDecl.id.name, s->funcDecl.id.length, true);
    }

    enterFunctionScope(c);

    // add phony variable for function receiver (in the case of functions the
    // receiver is the function itself but it ins't accessible)
    Identifier id = syntheticIdentifier("");
    addLocal(c, &id, s->line);

    foreach(n, s->funcDecl.formalArgs) {
        declareVar(c, (Identifier *)n->elem, s->line);
        defineVar(c, (Identifier *)n->elem, s->line);
    }

    if(s->funcDecl.isVararg) {
        Identifier args = syntheticIdentifier("args");
        declareVar(c, &args, s->line);
        defineVar(c, &args, s->line);
    }

    compileStatements(c, s->funcDecl.body->blockStmt.stmts);

    emitBytecode(c, OP_NULL, 0);
    emitBytecode(c, OP_RETURN, 0);

    exitFunctionScope(c);

    return c->func;
}

static ObjFunction *method(Compiler *c, ObjModule *module, Identifier *classId, Stmt *s) {
    size_t defaults = listLength(s->funcDecl.defArgs);
    size_t arity = listLength(s->funcDecl.formalArgs);

    c->func = newFunction(c->vm, module, NULL, arity, defaults);
    c->func->c.vararg = s->funcDecl.isVararg;

    // Phony const that will be set to the superclass of the method's class at runtime
    addConstant(&c->func->chunk, HANDLE_VAL(NULL));

    // Add default parameters
    addDefaultConsts(c, c->func->c.defaults, s->funcDecl.defArgs);

    // create new method name by concatenating the class name to it
    size_t length = classId->length + s->funcDecl.id.length + 1;
    ObjString *name = allocateString(c->vm, length);

    memcpy(name->data, classId->name, classId->length);
    name->data[classId->length] = '.';
    memcpy(name->data + classId->length + 1, s->funcDecl.id.name, s->funcDecl.id.length);

    c->func->c.name = name;

    // if in costructor change the type
    Identifier ctor = syntheticIdentifier(CTOR_STR);
    if(identifierEquals(&s->funcDecl.id, &ctor)) {
        c->type = TYPE_CTOR;
    }

    enterFunctionScope(c);

    // add `this` for method receiver (the object from which was called)
    Identifier thisId = syntheticIdentifier(THIS_STR);
    declareVar(c, &thisId, s->line);
    defineVar(c, &thisId, s->line);

    // define and declare arguments

    foreach(n, s->funcDecl.formalArgs) {
        declareVar(c, (Identifier *)n->elem, s->line);
        defineVar(c, (Identifier *)n->elem, s->line);
    }

    if(s->funcDecl.isVararg) {
        Identifier args = syntheticIdentifier("args");
        declareVar(c, &args, s->line);
        defineVar(c, &args, s->line);
    }

    compileStatements(c, s->funcDecl.body->blockStmt.stmts);

    // if in constructor return the instance
    if(c->type == TYPE_CTOR) {
        emitBytecode(c, OP_GET_LOCAL, 0);
        emitBytecode(c, 0, 0);
    } else {
        emitBytecode(c, OP_NULL, 0);
    }
    emitBytecode(c, OP_RETURN, 0);

    exitFunctionScope(c);

    return c->func;
}

static ObjString *readString(Compiler *c, Expr *e) {
    JStarBuffer sb;
    jsrBufferInit(c->vm, &sb);
    const char *str = e->str.str;

    for(size_t i = 0; i < e->str.length; i++) {
        char c = str[i];
        if(c == '\\') {
            switch(str[i + 1]) {
            case '0':
                jsrBufferAppendChar(&sb, '\0');
                break;
            case 'a':
                jsrBufferAppendChar(&sb, '\a');
                break;
            case 'b':
                jsrBufferAppendChar(&sb, '\b');
                break;
            case 'f':
                jsrBufferAppendChar(&sb, '\f');
                break;
            case 'n':
                jsrBufferAppendChar(&sb, '\n');
                break;
            case 'r':
                jsrBufferAppendChar(&sb, '\r');
                break;
            case 't':
                jsrBufferAppendChar(&sb, '\t');
                break;
            case 'v':
                jsrBufferAppendChar(&sb, '\v');
                break;
            case 'e':
                jsrBufferAppendChar(&sb, '\e');
                break;
            default:
                jsrBufferAppendChar(&sb, str[i + 1]);
                break;
            }
            i++;
        } else {
            jsrBufferAppendChar(&sb, c);
        }
    }

    return jsrBufferToString(&sb);
}

void reachCompilerRoots(JStarVM *vm, Compiler *c) {
    while(c != NULL) {
        reachObject(vm, (Obj *)c->func);
        c = c->prev;
    }
}
