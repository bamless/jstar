#include "compiler.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "code.h"
#include "common.h"
#include "jsrparse/lex.h"
#include "jstar.h"
#include "memory.h"
#include "opcode.h"
#include "value.h"
#include "vm.h"

// In case of a direct assignement of the form:
//  var a, b, ..., c = x, y, ..., z
// Where the right hand side is an unpackable object (i.e. a tuple or a list)
// We can omit the creation of the tuple/list, assigning directly the elements
// to the variables. We call this type of unpack assignement a 'const unpack'
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
    struct Loop* next;
} Loop;

typedef struct TryExcept {
    int depth;
    struct TryExcept* next;
} TryExcept;

typedef enum FuncType {
    TYPE_FUNC,
    TYPE_METHOD,
    TYPE_CTOR,
} FuncType;

struct Compiler {
    JStarVM* vm;
    Compiler* prev;
    const char* filename;

    bool hasSuper;

    Loop* loops;

    FuncType type;
    ObjFunction* func;
    Stmt* ast;

    uint8_t localsCount;
    Local locals[MAX_LOCALS];
    Upvalue upvalues[MAX_LOCALS];

    bool hadError;
    int depth;

    int tryDepth;
    TryExcept* tryBlocks;
};

static void initCompiler(Compiler* c, JStarVM* vm, const char* filename, Compiler* prev, FuncType t,
                         Stmt* ast) {
    c->vm = vm;
    c->type = t;
    c->ast = ast;
    c->depth = 0;
    c->func = NULL;
    c->prev = prev;
    c->loops = NULL;
    c->tryDepth = 0;
    c->localsCount = 0;
    c->hasSuper = false;
    c->hadError = false;
    c->tryBlocks = NULL;
    c->filename = filename;
    vm->currCompiler = c;
}

static void endCompiler(Compiler* c) {
    if(c->prev != NULL) c->prev->hadError |= c->hadError;
    c->vm->currCompiler = c->prev;
}

// -----------------------------------------------------------------------------
// UTILITY FUNCTIONS
// -----------------------------------------------------------------------------

static void error(Compiler* c, int line, const char* format, ...) {
    char errorMessage[MAX_ERR];
    va_list args;
    va_start(args, format);
    vsnprintf(errorMessage, MAX_ERR, format, args);
    va_end(args);

    JStarVM* vm = c->vm;
    if(vm->errorCallback) vm->errorCallback(c->filename, line, errorMessage);
    c->hadError = true;
}

static size_t emitBytecode(Compiler* c, uint8_t b, int line) {
    if(line == 0 && c->func->code.linesCount > 0) {
        line = c->func->code.lines[c->func->code.linesCount - 1];
    }
    return writeByte(&c->func->code, b, line);
}

static size_t emitShort(Compiler* c, uint16_t s, int line) {
    size_t i = emitBytecode(c, (uint8_t)(s >> 8), line);
    emitBytecode(c, (uint8_t)s, line);
    return i;
}

static void discardLocal(Compiler* c, Local* local) {
    if(local->isUpvalue) {
        emitBytecode(c, OP_CLOSE_UPVALUE, 0);
    } else {
        emitBytecode(c, OP_POP, 0);
    }
}

static void enterScope(Compiler* c) {
    c->depth++;
}

static void exitScope(Compiler* c) {
    c->depth--;
    while(c->localsCount > 0 && c->locals[c->localsCount - 1].depth > c->depth) {
        discardLocal(c, &c->locals[--c->localsCount]);
    }
}

static void discardScope(Compiler* c, int depth) {
    int localsCount = c->localsCount;
    while(localsCount > 0 && c->locals[localsCount - 1].depth > depth) {
        discardLocal(c, &c->locals[--localsCount]);
    }
}

static void enterFunctionScope(Compiler* c) {
    c->depth++;
}

static void exitFunctionScope(Compiler* c) {
    c->depth--;
}

static uint16_t createConst(Compiler* c, Value constant, int line) {
    int index = addConstant(&c->func->code, constant);
    if(index == -1) {
        const char* name = c->func->c.name == NULL ? "<main>" : c->func->c.name->data;
        error(c, line, "too many constants in function %s", name);
        return 0;
    }
    return (uint16_t)index;
}

static Identifier syntheticIdentifier(const char* name) {
    return (Identifier){strlen(name), name};
}

static uint16_t stringConst(Compiler* c, const char* str, size_t length, int line) {
    ObjString* idStr = copyString(c->vm, str, length, true);
    return createConst(c, OBJ_VAL(idStr), line);
}

static uint16_t identifierConst(Compiler* c, Identifier* id, int line) {
    return stringConst(c, id->name, id->length, line);
}

static void addLocal(Compiler* c, Identifier* id, int line) {
    if(c->localsCount == MAX_LOCALS) {
        error(c, line, "Too many local variables in function %s.", c->func->c.name->data);
        return;
    }
    Local* local = &c->locals[c->localsCount];
    local->isUpvalue = false;
    local->depth = -1;
    local->id = *id;
    c->localsCount++;
}

static int resolveVariable(Compiler* c, Identifier* id, bool inFunc, int line) {
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

static int addUpvalue(Compiler* c, uint8_t index, bool local, int line) {
    uint8_t upvalueCount = c->func->upvaluec;
    for(uint8_t i = 0; i < upvalueCount; i++) {
        Upvalue* upval = &c->upvalues[i];
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

static int resolveUpvalue(Compiler* c, Identifier* id, int line) {
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

static void declareVar(Compiler* c, Identifier* id, int line) {
    if(c->depth == 0) return;

    for(int i = c->localsCount - 1; i >= 0; i--) {
        if(c->locals[i].depth != -1 && c->locals[i].depth < c->depth) break;
        if(identifierEquals(&c->locals[i].id, id)) {
            error(c, line, "Variable `%.*s` already declared.", id->length, id->name);
        }
    }

    addLocal(c, id, line);
}

static void markInitialized(Compiler* c, int id) {
    ASSERT(id >= 0 && id < c->localsCount, "Invalid local variable");
    c->locals[id].depth = c->depth;
}

static void defineVar(Compiler* c, Identifier* id, int line) {
    if(c->depth == 0) {
        emitBytecode(c, OP_DEFINE_GLOBAL, line);
        emitShort(c, identifierConst(c, id, line), line);
    } else {
        markInitialized(c, c->localsCount - 1);
    }
}

static size_t emitJumpTo(Compiler* c, int jmpOpcode, size_t target, int line) {
    int32_t offset = target - (c->func->code.count + 3);
    if(offset > INT16_MAX || offset < INT16_MIN) {
        error(c, line, "Too much code to jump over.");
    }
    emitBytecode(c, jmpOpcode, 0);
    emitShort(c, (uint16_t)offset, 0);
    return c->func->code.count - 2;
}

static void setJumpTo(Compiler* c, size_t jumpAddr, size_t target, int line) {
    int32_t offset = target - (jumpAddr + 3);
    if(offset > INT16_MAX || offset < INT16_MIN) {
        error(c, line, "Too much code to jump over.");
    }
    Code* code = &c->func->code;
    code->bytecode[jumpAddr + 1] = (uint8_t)((uint16_t)offset >> 8);
    code->bytecode[jumpAddr + 2] = (uint8_t)((uint16_t)offset);
}

static void startLoop(Compiler* c, Loop* loop) {
    loop->depth = c->depth;
    loop->start = c->func->code.count;
    loop->next = c->loops;
    c->loops = loop;
}

static void patchLoopExitStmts(Compiler* c, size_t start, size_t cont, size_t brk) {
    for(size_t i = start; i < c->func->code.count; i++) {
        Opcode code = c->func->code.bytecode[i];
        if(code == OP_SIGN_BRK || code == OP_SIGN_CONT) {
            c->func->code.bytecode[i] = OP_JUMP;
            setJumpTo(c, i, code == OP_SIGN_CONT ? cont : brk, 0);
            code = OP_JUMP;
        }
        i += opcodeArgsNumber(code);
    }
}

static void endLoop(Compiler* c) {
    patchLoopExitStmts(c, c->loops->start, c->loops->start, c->func->code.count);
    c->loops = c->loops->next;
}

static void callMethod(Compiler* c, const char* name, int args) {
    Identifier meth = syntheticIdentifier(name);
    emitBytecode(c, OP_INVOKE_0 + args, 0);
    emitShort(c, identifierConst(c, &meth, 0), 0);
}

static void enterTryBlock(Compiler* c, TryExcept* tryExc, int numHandlers) {
    tryExc->depth = c->depth;
    tryExc->next = c->tryBlocks;
    c->tryBlocks = tryExc;
    c->tryDepth += numHandlers;
}

static void exitTryBlock(Compiler* c, int numHandlers) {
    c->tryBlocks = c->tryBlocks->next;
    c->tryDepth -= numHandlers;
}

static ObjString* readString(Compiler* c, Expr* e) {
    JStarBuffer sb;
    jsrBufferInit(c->vm, &sb);
    const char* str = e->as.string.str;

    for(size_t i = 0; i < e->as.string.length; i++) {
        char character = str[i];
        if(character == '\\') {
            switch(str[i + 1]) {
            case '0':
                jsrBufferAppendChar(&sb, '\0');
                break;
            case '\'':
                jsrBufferAppendChar(&sb, '\'');
                break;
            case '\\':
                jsrBufferAppendChar(&sb, '\\');
                break;
            case '"':
                jsrBufferAppendChar(&sb, '"');
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
            default:
                error(c, e->line, "Invalid escape character '%c'", str[i + 1]);
                break;
            }
            i++;
        } else {
            jsrBufferAppendChar(&sb, character);
        }
    }

    return jsrBufferToString(&sb);
}

static void addDefaultConsts(Compiler* c, Value* defaults, Vector* defArgs) {
    int i = 0;
    vecForeach(Expr(**it), *defArgs) {
        Expr* e = *it;
        switch(e->type) {
        case NUM_LIT:
            defaults[i++] = NUM_VAL(e->as.num);
            break;
        case BOOL_LIT:
            defaults[i++] = BOOL_VAL(e->as.boolean);
            break;
        case STR_LIT:
            defaults[i++] = OBJ_VAL(readString(c, e));
            break;
        case NULL_LIT:
            defaults[i++] = NULL_VAL;
            break;
        default:
            UNREACHABLE();
            break;
        }
    }
}

// -----------------------------------------------------------------------------
// EXPRESSION COMPILE
// -----------------------------------------------------------------------------

static void compileExpr(Compiler* c, Expr* e);

static void compileBinaryExpr(Compiler* c, Expr* e) {
    compileExpr(c, e->as.binary.left);
    compileExpr(c, e->as.binary.right);
    switch(e->as.binary.op) {
    case TOK_PLUS:
        emitBytecode(c, OP_ADD, e->line);
        break;
    case TOK_MINUS:
        emitBytecode(c, OP_SUB, e->line);
        break;
    case TOK_MULT:
        emitBytecode(c, OP_MUL, e->line);
        break;
    case TOK_DIV:
        emitBytecode(c, OP_DIV, e->line);
        break;
    case TOK_MOD:
        emitBytecode(c, OP_MOD, e->line);
        break;
    case TOK_EQUAL_EQUAL:
        emitBytecode(c, OP_EQ, e->line);
        break;
    case TOK_GT:
        emitBytecode(c, OP_GT, e->line);
        break;
    case TOK_GE:
        emitBytecode(c, OP_GE, e->line);
        break;
    case TOK_LT:
        emitBytecode(c, OP_LT, e->line);
        break;
    case TOK_LE:
        emitBytecode(c, OP_LE, e->line);
        break;
    case TOK_IS:
        emitBytecode(c, OP_IS, e->line);
        break;
    case TOK_BANG_EQ:
        emitBytecode(c, OP_EQ, e->line);
        emitBytecode(c, OP_NOT, e->line);
        break;
    default:
        UNREACHABLE();
        break;
    }
}

static void compileLogicExpr(Compiler* c, Expr* e) {
    compileExpr(c, e->as.binary.left);
    emitBytecode(c, OP_DUP, e->line);

    uint8_t jmp = e->as.binary.op == TOK_AND ? OP_JUMPF : OP_JUMPT;
    size_t shortCircuit = emitBytecode(c, jmp, 0);
    emitShort(c, 0, 0);

    emitBytecode(c, OP_POP, e->line);
    compileExpr(c, e->as.binary.right);

    setJumpTo(c, shortCircuit, c->func->code.count, e->line);
}

static void compileUnaryExpr(Compiler* c, Expr* e) {
    compileExpr(c, e->as.unary.operand);
    switch(e->as.unary.op) {
    case TOK_MINUS:
        emitBytecode(c, OP_NEG, e->line);
        break;
    case TOK_BANG:
        emitBytecode(c, OP_NOT, e->line);
        break;
    case TOK_HASH:
        callMethod(c, "__len__", 0);
        break;
    case TOK_HASH_HASH:
        callMethod(c, "__string__", 0);
        break;
    default:
        UNREACHABLE();
        break;
    }
}

static void compileTernaryExpr(Compiler* c, Expr* e) {
    compileExpr(c, e->as.ternary.cond);

    size_t falseJmp = emitBytecode(c, OP_JUMPF, e->line);
    emitShort(c, 0, 0);

    compileExpr(c, e->as.ternary.thenExpr);
    size_t exitJmp = emitBytecode(c, OP_JUMP, e->line);
    emitShort(c, 0, 0);

    setJumpTo(c, falseJmp, c->func->code.count, e->line);
    compileExpr(c, e->as.ternary.elseExpr);

    setJumpTo(c, exitJmp, c->func->code.count, e->line);
}

static void compileVariable(Compiler* c, Identifier* id, bool set, int line) {
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

static void compileFunction(Compiler* c, Stmt* s);

static void compileAnonymousFunc(Compiler* c, Identifier* name, Expr* e) {
    Stmt* f = e->as.anonFunc.func;
    if(name == NULL) {
        char funcName[5 + STRLEN_FOR_INT(int) + 1];
        sprintf(funcName, ANON_PREFIX "%d", f->line);
        f->as.funcDecl.id.length = strlen(funcName);
        f->as.funcDecl.id.name = funcName;
        compileFunction(c, f);
    } else {
        f->as.funcDecl.id.length = name->length;
        f->as.funcDecl.id.name = name->name;
        compileFunction(c, f);
    }
}

static void compileLval(Compiler* c, Expr* e) {
    switch(e->type) {
    case VAR_LIT:
        compileVariable(c, &e->as.var.id, true, e->line);
        break;
    case ACCESS_EXPR: {
        compileExpr(c, e->as.access.left);
        emitBytecode(c, OP_SET_FIELD, e->line);
        emitShort(c, identifierConst(c, &e->as.access.id, e->line), e->line);
        break;
    }
    case ARR_ACC: {
        compileExpr(c, e->as.arrayAccess.index);
        compileExpr(c, e->as.arrayAccess.left);
        emitBytecode(c, OP_SUBSCR_SET, e->line);
        break;
    }
    default:
        UNREACHABLE();
        break;
    }
}

static void compileRval(Compiler* c, Identifier* boundName, Expr* e) {
    if(e->type == ANON_FUNC)
        compileAnonymousFunc(c, boundName, e);
    else
        compileExpr(c, e);
}

static void compileConstUnpackLst(Compiler* c, Vector* boundNames, Expr* exprs, int num) {
    int i = 0;
    vecForeach(Expr(**e), exprs->as.list) {
        Identifier* boundName = boundNames ? vecGet(boundNames, i) : NULL;
        compileRval(c, boundName, *e);
        if(++i > num) emitBytecode(c, OP_POP, 0);
    }
    if(i < num) {
        error(c, exprs->line, "Too little values to unpack.");
    }
}

// Compile an unpack assignment of the form: a, b, ..., z = ...
static void compileUnpackAssign(Compiler* c, Expr* e) {
    Expr* tupleExprs = e->as.assign.lval->as.tuple.exprs;
    size_t tupleSize = vecSize(&tupleExprs->as.list);

    if(tupleSize >= UINT8_MAX) {
        error(c, e->line, "Exceeded max number of unpack assignment: %d.", UINT8_MAX);
    }

    Expr* rval = e->as.assign.rval;
    if(IS_CONST_UNPACK(rval->type)) {
        Expr* lst = rval->type == ARR_LIT ? rval->as.array.exprs : rval->as.tuple.exprs;
        compileConstUnpackLst(c, NULL, lst, tupleSize);
    } else {
        compileRval(c, NULL, rval);
        emitBytecode(c, OP_UNPACK, e->line);
        emitBytecode(c, (uint8_t)tupleSize, e->line);
    }

    // compile lvals in reverse order in order to assign
    // correct values to variables in case of a const unpack
    for(int n = tupleSize - 1; n >= 0; n--) {
        Expr* lval = vecGet(&tupleExprs->as.list, n);
        compileLval(c, lval);
        if(n != 0) emitBytecode(c, OP_POP, e->line);
    }
}

static void compileAssignExpr(Compiler* c, Expr* e) {
    switch(e->as.assign.lval->type) {
    case VAR_LIT: {
        Identifier* name = &e->as.assign.lval->as.var.id;
        compileRval(c, name, e->as.assign.rval);
        compileLval(c, e->as.assign.lval);
        break;
    }
    case ACCESS_EXPR: {
        Identifier* name = &e->as.assign.lval->as.access.id;
        compileRval(c, name, e->as.assign.rval);
        compileLval(c, e->as.assign.lval);
        break;
    }
    case ARR_ACC: {
        compileRval(c, NULL, e->as.assign.rval);
        compileLval(c, e->as.assign.lval);
        break;
    }
    case TUPLE_LIT: {
        compileUnpackAssign(c, e);
        break;
    }
    default:
        UNREACHABLE();
        break;
    }
}

static void compileCompundAssign(Compiler* c, Expr* e) {
    TokenType op = e->as.compound.op;
    Expr* l = e->as.compound.lval;
    Expr* r = e->as.compound.rval;

    // expand compound assignement (e.g. a op= b -> a = a op b)
    Expr binary = {e->line, BINARY, .as = {.binary = {op, l, r}}};
    Expr assignment = {e->line, ASSIGN, .as = {.assign = {l, &binary}}};

    // compile as a normal assignment
    compileAssignExpr(c, &assignment);
}

static void compileCallExpr(Compiler* c, Expr* e) {
    Opcode callCode = OP_CALL;
    Opcode callInline = OP_CALL_0;

    Expr* callee = e->as.call.callee;
    bool isMethod = callee->type == ACCESS_EXPR;

    if(isMethod) {
        callCode = OP_INVOKE;
        callInline = OP_INVOKE_0;
        compileExpr(c, callee->as.access.left);
    } else {
        compileExpr(c, callee);
    }

    Expr* argsList = e->as.call.args;
    vecForeach(Expr(**it), argsList->as.list) {
        // Compile argument list
        compileExpr(c, *it);
    }

    size_t argsCount = vecSize(&argsList->as.list);
    if(argsCount >= UINT8_MAX) {
        error(c, e->line, "Too many arguments for function %s.", c->func->c.name->data);
    }

    if(argsCount <= 10) {
        emitBytecode(c, callInline + argsCount, e->line);
    } else {
        emitBytecode(c, callCode, e->line);
        emitBytecode(c, argsCount, e->line);
    }

    if(isMethod) {
        emitShort(c, identifierConst(c, &callee->as.access.id, e->line), e->line);
    }
}

static void compileSuper(Compiler* c, Expr* e) {
    if(c->type != TYPE_METHOD && c->type != TYPE_CTOR) {
        error(c, e->line, "Can only use `super` in method call");
        return;
    }

    emitBytecode(c, OP_GET_LOCAL, e->line);
    emitBytecode(c, 0, e->line);

    uint16_t nameConst;
    if(e->as.sup.name.name) {
        nameConst = identifierConst(c, &e->as.sup.name, e->line);
    } else {
        nameConst = identifierConst(c, &c->ast->as.funcDecl.id, e->line);
    }

    if(e->as.sup.args != NULL) {
        Expr* argsList = e->as.sup.args;
        vecForeach(Expr(**it), argsList->as.list) {
            // Compile argument list
            compileExpr(c, *it);
        }

        size_t argsCount = vecSize(&argsList->as.list);
        if(argsCount >= UINT8_MAX) {
            error(c, e->line, "Too many arguments for function %s.", c->func->c.name->data);
        }

        if(argsCount <= 10) {
            emitBytecode(c, OP_SUPER_0 + argsCount, e->line);
        } else {
            emitBytecode(c, OP_SUPER, e->line);
            emitBytecode(c, argsCount, e->line);
        }

        emitShort(c, nameConst, e->line);
    } else {
        emitBytecode(c, OP_SUPER_BIND, e->line);
        emitShort(c, nameConst, e->line);
    }
}

static void compileAccessExpression(Compiler* c, Expr* e) {
    compileExpr(c, e->as.access.left);
    emitBytecode(c, OP_GET_FIELD, e->line);
    emitShort(c, identifierConst(c, &e->as.access.id, e->line), e->line);
}

static void compileArraryAccExpression(Compiler* c, Expr* e) {
    compileExpr(c, e->as.arrayAccess.left);
    compileExpr(c, e->as.arrayAccess.index);
    emitBytecode(c, OP_SUBSCR_GET, e->line);
}

static void compileExpExpr(Compiler* c, Expr* e) {
    compileExpr(c, e->as.exponent.base);
    compileExpr(c, e->as.exponent.exp);
    emitBytecode(c, OP_POW, e->line);
}

static void compileArrayLit(Compiler* c, Expr* e) {
    emitBytecode(c, OP_NEW_LIST, e->line);
    vecForeach(Expr(**it), e->as.array.exprs->as.list) {
        compileExpr(c, *it);
        emitBytecode(c, OP_APPEND_LIST, e->line);
    }
}

static void compileTupleLit(Compiler* c, Expr* e) {
    vecForeach(Expr(**it), e->as.tuple.exprs->as.list) {
        // Compile tuple
        compileExpr(c, *it);
    }
    size_t tupleSize = vecSize(&e->as.tuple.exprs->as.list);
    if(tupleSize >= UINT8_MAX) error(c, e->line, "Too many elements in tuple literal.");
    emitBytecode(c, OP_NEW_TUPLE, e->line);
    emitBytecode(c, tupleSize, e->line);
}

static void compileTableLit(Compiler* c, Expr* e) {
    emitBytecode(c, OP_NEW_TABLE, e->line);

    Expr* keyVals = e->as.table.keyVals;
    for(Expr** it = vecBegin(&keyVals->as.list); it != vecEnd(&keyVals->as.list);) {
        Expr* key = *it;
        Expr* val = *(it + 1);

        emitBytecode(c, OP_DUP, e->line);
        compileExpr(c, key);
        compileExpr(c, val);
        callMethod(c, "__set__", 2);
        emitBytecode(c, OP_POP, e->line);

        it += 2;
    }
}

static void emitValueConst(Compiler* c, Value val, int line) {
    emitBytecode(c, OP_GET_CONST, line);
    emitShort(c, createConst(c, val, line), line);
}

static void compileExpr(Compiler* c, Expr* e) {
    switch(e->type) {
    case BINARY:
        if(e->as.binary.op == TOK_AND || e->as.binary.op == TOK_OR) {
            compileLogicExpr(c, e);
        } else {
            compileBinaryExpr(c, e);
        }
        break;
    case ASSIGN:
        compileAssignExpr(c, e);
        break;
    case COMP_ASSIGN:
        compileCompundAssign(c, e);
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
    case EXPR_LST:
        vecForeach(Expr(**it), e->as.list) { compileExpr(c, *it); }
        break;
    case NUM_LIT:
        emitValueConst(c, NUM_VAL(e->as.num), e->line);
        break;
    case BOOL_LIT:
        emitValueConst(c, BOOL_VAL(e->as.boolean), e->line);
        break;
    case STR_LIT: {
        emitValueConst(c, OBJ_VAL(readString(c, e)), e->line);
        break;
    }
    case VAR_LIT:
        compileVariable(c, &e->as.var.id, false, e->line);
        break;
    case NULL_LIT:
        emitBytecode(c, OP_NULL, e->line);
        break;
    case ARR_LIT:
        compileArrayLit(c, e);
        break;
    case TUPLE_LIT:
        compileTupleLit(c, e);
        break;
    case TABLE_LIT:
        compileTableLit(c, e);
        break;
    case SUPER_LIT:
        compileSuper(c, e);
        break;
    case ANON_FUNC:
        compileAnonymousFunc(c, NULL, e);
        break;
    }
}

// -----------------------------------------------------------------------------
// STATEMENT COMPILE
// -----------------------------------------------------------------------------

static void compileStatement(Compiler* c, Stmt* s);

static void compileStatements(Compiler* c, Vector* stmts) {
    vecForeach(Stmt(**it), *stmts) { compileStatement(c, *it); }
}

static void compileVarDecl(Compiler* c, Stmt* s) {
    vecForeach(Identifier(**it), s->as.varDecl.ids) {
        Identifier* name = *it;
        declareVar(c, name, s->line);
    }

    int numDecls = vecSize(&s->as.varDecl.ids);
    if(s->as.varDecl.init != NULL) {
        Expr* init = s->as.varDecl.init;

        if(s->as.varDecl.isUnpack && IS_CONST_UNPACK(init->type)) {
            Expr* exprs = init->type == ARR_LIT ? init->as.array.exprs : init->as.tuple.exprs;
            compileConstUnpackLst(c, &s->as.varDecl.ids, exprs, numDecls);
        } else {
            compileRval(c, vecGet(&s->as.varDecl.ids, 0), init);
            if(s->as.varDecl.isUnpack) {
                emitBytecode(c, OP_UNPACK, s->line);
                emitBytecode(c, (uint8_t)numDecls, s->line);
            }
        }
    } else {
        for(int i = 0; i < numDecls; i++) {
            emitBytecode(c, OP_NULL, s->line);
        }
    }

    // define in reverse order in order to assign correct
    // values to variables in case of a const unpack
    for(int i = numDecls - 1; i >= 0; i--) {
        if(c->depth == 0) {
            defineVar(c, vecGet(&s->as.varDecl.ids, i), s->line);
        } else {
            markInitialized(c, c->localsCount - i - 1);
        }
    }
}

static void compileReturnStatement(Compiler* c, Stmt* s) {
    if(c->prev == NULL) {
        error(c, s->line, "Cannot use return in global scope.");
    }
    if(c->type == TYPE_CTOR) {
        error(c, s->line, "Cannot use return in constructor.");
    }

    if(s->as.returnStmt.e != NULL) {
        compileExpr(c, s->as.returnStmt.e);
    } else {
        emitBytecode(c, OP_NULL, s->line);
    }

    emitBytecode(c, OP_RETURN, s->line);
}

static void compileIfStatement(Compiler* c, Stmt* s) {
    compileExpr(c, s->as.ifStmt.cond);

    size_t falseJmp = emitBytecode(c, OP_JUMPF, 0);
    emitShort(c, 0, 0);

    compileStatement(c, s->as.ifStmt.thenStmt);

    size_t exitJmp = 0;
    if(s->as.ifStmt.elseStmt != NULL) {
        exitJmp = emitBytecode(c, OP_JUMP, 0);
        emitShort(c, 0, 0);
    }

    setJumpTo(c, falseJmp, c->func->code.count, s->line);

    if(s->as.ifStmt.elseStmt != NULL) {
        compileStatement(c, s->as.ifStmt.elseStmt);
        setJumpTo(c, exitJmp, c->func->code.count, s->line);
    }
}

static void compileForStatement(Compiler* c, Stmt* s) {
    enterScope(c);

    if(s->as.forStmt.init != NULL) {
        compileStatement(c, s->as.forStmt.init);
    }

    size_t firstJmp = 0;
    if(s->as.forStmt.act != NULL) {
        firstJmp = emitBytecode(c, OP_JUMP, s->line);
        emitShort(c, 0, 0);
    }

    Loop l;
    startLoop(c, &l);

    if(s->as.forStmt.act != NULL) {
        compileExpr(c, s->as.forStmt.act);
        emitBytecode(c, OP_POP, 0);
        setJumpTo(c, firstJmp, c->func->code.count, s->line);
    }

    size_t exitJmp = 0;
    if(s->as.forStmt.cond != NULL) {
        compileExpr(c, s->as.forStmt.cond);
        exitJmp = emitBytecode(c, OP_JUMPF, 0);
        emitShort(c, 0, 0);
    }

    compileStatement(c, s->as.forStmt.body);
    emitJumpTo(c, OP_JUMP, l.start, 0);

    if(s->as.forStmt.cond != NULL) {
        setJumpTo(c, exitJmp, c->func->code.count, s->line);
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
 */
static void compileForEach(Compiler* c, Stmt* s) {
    enterScope(c);

    Identifier expr = syntheticIdentifier(".expr");
    declareVar(c, &expr, s->as.forEach.iterable->line);
    defineVar(c, &expr, s->as.forEach.iterable->line);

    compileExpr(c, s->as.forEach.iterable);

    // set the iterator variable with a name that it's not an identifier.
    // this will avoid the user shadowing the iterator with a declared variable.
    Identifier iterator = syntheticIdentifier(".iter");
    declareVar(c, &iterator, s->line);
    defineVar(c, &iterator, s->line);

    emitBytecode(c, OP_NULL, 0);

    Loop l;
    startLoop(c, &l);

    emitBytecode(c, OP_FOR_ITER, s->line);
    compileVariable(c, &iterator, true, s->line);
    size_t exitJmp = emitBytecode(c, OP_FOR_NEXT, 0);
    emitShort(c, 0, 0);

    Stmt* varDecl = s->as.forEach.var;
    enterScope(c);

    vecForeach(Identifier(**id), varDecl->as.varDecl.ids) {
        declareVar(c, *id, s->line);
        defineVar(c, *id, s->line);
    }

    int num = vecSize(&varDecl->as.varDecl.ids);
    if(varDecl->as.varDecl.isUnpack) {
        emitBytecode(c, OP_UNPACK, s->line);
        emitBytecode(c, (uint8_t)num, s->line);
    }

    Stmt* body = s->as.forEach.body;
    compileStatements(c, &body->as.blockStmt.stmts);

    exitScope(c);

    emitJumpTo(c, OP_JUMP, l.start, 0);
    setJumpTo(c, exitJmp, c->func->code.count, s->line);

    endLoop(c);
    exitScope(c);
}

static void compileWhileStatement(Compiler* c, Stmt* s) {
    Loop l;
    startLoop(c, &l);

    compileExpr(c, s->as.whileStmt.cond);
    size_t exitJmp = emitBytecode(c, OP_JUMPF, 0);
    emitShort(c, 0, 0);

    compileStatement(c, s->as.whileStmt.body);

    emitJumpTo(c, OP_JUMP, l.start, 0);
    setJumpTo(c, exitJmp, c->func->code.count, s->line);

    endLoop(c);
}

static void compileImportStatement(Compiler* c, Stmt* s) {
    Vector* modules = &s->as.importStmt.modules;
    Vector* impNames = &s->as.importStmt.impNames;
    bool isImportFor = vecSize(impNames);
    bool isImportAs = s->as.importStmt.as.name != NULL;

    JStarBuffer moduleName;
    jsrBufferInit(c->vm, &moduleName);

    // compile topmost import
    Identifier* moduleId = (Identifier*)vecGet(modules, 0);
    jsrBufferAppend(&moduleName, moduleId->name, moduleId->length);
    if(!isImportAs && !isImportFor) {
        emitBytecode(c, OP_IMPORT, s->line);
    } else {
        emitBytecode(c, OP_IMPORT_FROM, s->line);
    }
    emitShort(c, stringConst(c, moduleName.data, moduleName.len, s->line), s->line);

    // compile submodule imports
    for(size_t i = 1; i < vecSize(modules); i++) {
        // pop previous import result
        emitBytecode(c, OP_POP, s->line);

        Identifier* subModuleId = (Identifier*)vecGet(modules, i);
        jsrBufferAppendf(&moduleName, ".%.*s", subModuleId->length, subModuleId->name);

        emitBytecode(c, OP_IMPORT_FROM, s->line);
        emitShort(c, stringConst(c, moduleName.data, moduleName.len, s->line), s->line);
    }

    if(isImportFor) {
        uint16_t moduleNameConst = stringConst(c, moduleName.data, moduleName.len, s->line);
        vecForeach(Identifier(**it), *impNames) {
            Identifier* name = *it;
            emitBytecode(c, OP_IMPORT_NAME, s->line);
            emitShort(c, moduleNameConst, s->line);
            emitShort(c, identifierConst(c, name, s->line), s->line);
        }
    } else if(isImportAs) {
        // set last import as an import as
        c->func->code.bytecode[c->func->code.count - 3] = OP_IMPORT_AS;
        emitShort(c, identifierConst(c, &s->as.importStmt.as, s->line), s->line);
    }

    emitBytecode(c, OP_POP, s->line);
    jsrBufferFree(&moduleName);
}

static void compileExcepts(Compiler* c, Vector* excs, int n) {
    Stmt* exc = vecGet(excs, n);
    bool last = n == (int)(vecSize(excs) - 1);

    Identifier exception = syntheticIdentifier(".exception");
    compileVariable(c, &exception, false, exc->line);
    compileExpr(c, exc->as.excStmt.cls);
    emitBytecode(c, OP_IS, 0);

    size_t falseJmp = emitBytecode(c, OP_JUMPF, 0);
    emitShort(c, 0, 0);

    enterScope(c);

    compileVariable(c, &exception, false, exc->line);
    declareVar(c, &exc->as.excStmt.var, exc->line);
    defineVar(c, &exc->as.excStmt.var, exc->line);

    Stmt* excBody = exc->as.excStmt.block;
    compileStatements(c, &excBody->as.blockStmt.stmts);

    emitBytecode(c, OP_NULL, exc->line);
    compileVariable(c, &exception, true, exc->line);
    emitBytecode(c, OP_POP, exc->line);

    exitScope(c);

    size_t exitJmp = 0;
    if(!last) {
        exitJmp = emitBytecode(c, OP_JUMP, 0);
        emitShort(c, 0, 0);
    }

    setJumpTo(c, falseJmp, c->func->code.count, exc->line);

    if(!last) {
        compileExcepts(c, excs, n + 1);
        setJumpTo(c, exitJmp, c->func->code.count, exc->line);
    }
}

static void compileTryExcept(Compiler* c, Stmt* s) {
    bool hasExcept = !vecEmpty(&s->as.tryStmt.excs);
    bool hasEnsure = s->as.tryStmt.ensure != NULL;

    TryExcept tryBlock;
    int numHandlers = (int)hasExcept + (int)hasEnsure;
    enterTryBlock(c, &tryBlock, numHandlers);

    if(c->tryDepth > MAX_TRY_DEPTH) {
        error(c, s->line, "Exceeded max number of nested try blocks (%d)", MAX_TRY_DEPTH);
    }

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

    compileStatement(c, s->as.tryStmt.block);

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

        setJumpTo(c, excSetup, c->func->code.count, s->line);

        compileExcepts(c, &s->as.tryStmt.excs, 0);

        if(hasEnsure) {
            emitBytecode(c, OP_POP_HANDLER, 0);
        } else {
            emitBytecode(c, OP_END_TRY, 0);
            exitScope(c);
        }

        setJumpTo(c, excJmp, c->func->code.count, 0);
    }

    if(hasEnsure) {
        setJumpTo(c, ensSetup, c->func->code.count, s->line);
        compileStatements(c, &s->as.tryStmt.ensure->as.blockStmt.stmts);
        emitBytecode(c, OP_END_TRY, 0);
        exitScope(c);
    }

    exitTryBlock(c, numHandlers);
}

static void compileRaiseStmt(Compiler* c, Stmt* s) {
    compileExpr(c, s->as.raiseStmt.exc);
    emitBytecode(c, OP_RAISE, s->line);
}

/*
 * with Expr x
 *   code
 * end
 *
 * begin
 *   var x
 *   try
 *     x = Expr
 *     code
 *   ensure
 *     if x then x.close() end
 *   end
 * end
 */
static void compileWithStatement(Compiler* c, Stmt* s) {
    enterScope(c);

    // var x
    emitBytecode(c, OP_NULL, s->line);
    declareVar(c, &s->as.withStmt.var, s->line);
    defineVar(c, &s->as.withStmt.var, s->line);

    // try
    TryExcept tryBlock;
    enterTryBlock(c, &tryBlock, 1);

    if(c->tryDepth > MAX_TRY_DEPTH) {
        error(c, s->line, "Exceeded max number of nested try blocks (%d)", MAX_TRY_DEPTH);
    }

    size_t ensSetup = emitBytecode(c, OP_SETUP_ENSURE, s->line);
    emitShort(c, 0, 0);

    // x = closable
    Expr lval = {.line = s->line, .type = VAR_LIT, .as = {.var = {s->as.withStmt.var}}};
    Expr assign = {.line = s->line,
                   .type = ASSIGN,
                   .as = {.assign = {.lval = &lval, .rval = s->as.withStmt.e}}};
    compileExpr(c, &assign);
    emitBytecode(c, OP_POP, s->line);

    // code
    compileStatement(c, s->as.withStmt.block);

    emitBytecode(c, OP_POP_HANDLER, s->line);
    emitBytecode(c, OP_NULL, s->line);
    emitBytecode(c, OP_NULL, s->line);

    // ensure
    enterScope(c);

    Identifier exc = syntheticIdentifier(".exception");
    declareVar(c, &exc, 0);
    defineVar(c, &exc, 0);

    Identifier cause = syntheticIdentifier(".cause");
    declareVar(c, &cause, 0);
    defineVar(c, &cause, 0);

    setJumpTo(c, ensSetup, c->func->code.count, s->line);

    // if x then x.close() end
    compileVariable(c, &s->as.withStmt.var, false, s->line);
    size_t falseJmp = emitBytecode(c, OP_JUMPF, s->line);
    emitShort(c, 0, 0);

    compileVariable(c, &s->as.withStmt.var, false, s->line);
    callMethod(c, "close", 0);
    emitBytecode(c, OP_POP, s->line);

    setJumpTo(c, falseJmp, c->func->code.count, s->line);

    emitBytecode(c, OP_END_TRY, 0);
    exitScope(c);

    exitTryBlock(c, 1);
    exitScope(c);
}

static void compileLoopExitStmt(Compiler* c, Stmt* s) {
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

static ObjFunction* function(Compiler* c, ObjModule* module, Stmt* s) {
    size_t defaults = vecSize(&s->as.funcDecl.defArgs);
    size_t arity = vecSize(&s->as.funcDecl.formalArgs);
    bool vararg = s->as.funcDecl.isVararg;

    c->func = newFunction(c->vm, module, NULL, arity, defaults, vararg);
    addDefaultConsts(c, c->func->c.defaults, &s->as.funcDecl.defArgs);

    if(s->as.funcDecl.id.length != 0) {
        c->func->c.name = copyString(c->vm, s->as.funcDecl.id.name, s->as.funcDecl.id.length, true);
    }

    // add phony variable for function receiver (in the case of functions the
    // receiver is the function itself but it ins't accessible)
    Identifier id = syntheticIdentifier("");
    addLocal(c, &id, s->line);

    vecForeach(Identifier(**it), s->as.funcDecl.formalArgs) {
        declareVar(c, *it, s->line);
        defineVar(c, *it, s->line);
    }

    if(s->as.funcDecl.isVararg) {
        Identifier args = syntheticIdentifier("args");
        declareVar(c, &args, s->line);
        defineVar(c, &args, s->line);
    }

    Stmt* body = s->as.funcDecl.body;
    compileStatements(c, &body->as.blockStmt.stmts);

    emitBytecode(c, OP_NULL, 0);
    emitBytecode(c, OP_RETURN, 0);

    return c->func;
}

static ObjFunction* method(Compiler* c, ObjModule* module, Identifier* classId, Stmt* s) {
    size_t defaults = vecSize(&s->as.funcDecl.defArgs);
    size_t arity = vecSize(&s->as.funcDecl.formalArgs);
    bool vararg = s->as.funcDecl.isVararg;

    c->func = newFunction(c->vm, module, NULL, arity, defaults, vararg);

    // Phony const that will be set to the superclass of the method's class at runtime
    addConstant(&c->func->code, HANDLE_VAL(NULL));
    addDefaultConsts(c, c->func->c.defaults, &s->as.funcDecl.defArgs);

    // create new method name by concatenating the class name to it
    size_t length = classId->length + s->as.funcDecl.id.length + 1;
    ObjString* name = allocateString(c->vm, length);

    memcpy(name->data, classId->name, classId->length);
    name->data[classId->length] = '.';
    memcpy(name->data + classId->length + 1, s->as.funcDecl.id.name, s->as.funcDecl.id.length);
    c->func->c.name = name;

    // if in costructor change the type
    Identifier ctor = syntheticIdentifier(CTOR_STR);
    if(identifierEquals(&s->as.funcDecl.id, &ctor)) {
        c->type = TYPE_CTOR;
    }

    // add `this` for method receiver (the object from which was called)
    Identifier thisId = syntheticIdentifier(THIS_STR);
    declareVar(c, &thisId, s->line);
    defineVar(c, &thisId, s->line);

    // define and declare arguments

    vecForeach(Identifier(**it), s->as.funcDecl.formalArgs) {
        declareVar(c, *it, s->line);
        defineVar(c, *it, s->line);
    }

    if(s->as.funcDecl.isVararg) {
        Identifier args = syntheticIdentifier("args");
        declareVar(c, &args, s->line);
        defineVar(c, &args, s->line);
    }

    Stmt* body = s->as.funcDecl.body;
    compileStatements(c, &body->as.blockStmt.stmts);

    // if in constructor return the instance
    if(c->type == TYPE_CTOR) {
        emitBytecode(c, OP_GET_LOCAL, 0);
        emitBytecode(c, 0, 0);
    } else {
        emitBytecode(c, OP_NULL, 0);
    }
    emitBytecode(c, OP_RETURN, 0);

    return c->func;
}

static void compileFunction(Compiler* c, Stmt* s) {
    Compiler funCompiler;
    initCompiler(&funCompiler, c->vm, c->filename, c, TYPE_FUNC, s);

    enterFunctionScope(&funCompiler);
    ObjFunction* func = function(&funCompiler, c->func->c.module, s);
    exitFunctionScope(&funCompiler);

    emitBytecode(c, OP_CLOSURE, s->line);
    emitShort(c, createConst(c, OBJ_VAL(func), s->line), s->line);

    for(uint8_t i = 0; i < func->upvaluec; i++) {
        emitBytecode(c, funCompiler.upvalues[i].isLocal ? 1 : 0, s->line);
        emitBytecode(c, funCompiler.upvalues[i].index, s->line);
    }

    endCompiler(&funCompiler);
}

static void compileNative(Compiler* c, Stmt* s) {
    size_t defaults = vecSize(&s->as.nativeDecl.defArgs);
    size_t arity = vecSize(&s->as.nativeDecl.formalArgs);
    bool vararg = s->as.nativeDecl.isVararg;

    ObjNative* native = newNative(c->vm, c->func->c.module, NULL, arity, NULL, defaults, vararg);

    push(c->vm, OBJ_VAL(native));
    addDefaultConsts(c, native->c.defaults, &s->as.nativeDecl.defArgs);
    pop(c->vm);

    uint16_t nativeConst = createConst(c, OBJ_VAL(native), s->line);
    uint16_t nameConst = identifierConst(c, &s->as.nativeDecl.id, s->line);
    native->c.name = AS_STRING(c->func->code.consts.arr[nameConst]);

    emitBytecode(c, OP_GET_CONST, s->line);
    emitShort(c, nativeConst, s->line);

    emitBytecode(c, OP_NATIVE, s->line);
    emitShort(c, nameConst, s->line);
}

static void compileMethod(Compiler* c, Stmt* cls, Stmt* m) {
    Compiler methodCompiler;
    initCompiler(&methodCompiler, c->vm, c->filename, c, TYPE_METHOD, m);

    enterFunctionScope(&methodCompiler);
    ObjFunction* meth = method(&methodCompiler, c->func->c.module, &cls->as.classDecl.id, m);
    exitFunctionScope(&methodCompiler);

    emitBytecode(c, OP_CLOSURE, m->line);
    emitShort(c, createConst(c, OBJ_VAL(meth), m->line), m->line);

    for(uint8_t i = 0; i < meth->upvaluec; i++) {
        emitBytecode(c, methodCompiler.upvalues[i].isLocal ? 1 : 0, m->line);
        emitBytecode(c, methodCompiler.upvalues[i].index, m->line);
    }

    emitBytecode(c, OP_DEF_METHOD, cls->line);
    emitShort(c, identifierConst(c, &m->as.funcDecl.id, m->line), cls->line);
    endCompiler(&methodCompiler);
}

static void compileNativeMethod(Compiler* c, Stmt* cls, Stmt* m) {
    size_t defaults = vecSize(&m->as.nativeDecl.defArgs);
    size_t arity = vecSize(&m->as.nativeDecl.formalArgs);
    bool vararg = m->as.nativeDecl.isVararg;

    ObjNative* n = newNative(c->vm, c->func->c.module, NULL, arity, NULL, defaults, vararg);
    push(c->vm, OBJ_VAL(n));
    addDefaultConsts(c, n->c.defaults, &m->as.nativeDecl.defArgs);
    pop(c->vm);

    uint16_t native = createConst(c, OBJ_VAL(n), cls->line);
    uint16_t id = identifierConst(c, &m->as.nativeDecl.id, m->line);

    Identifier* classId = &cls->as.classDecl.id;
    size_t len = classId->length + m->as.nativeDecl.id.length + 1;
    ObjString* name = allocateString(c->vm, len);

    memcpy(name->data, classId->name, classId->length);
    name->data[classId->length] = '.';
    memcpy(name->data + classId->length + 1, m->as.nativeDecl.id.name, m->as.nativeDecl.id.length);
    n->c.name = name;

    emitBytecode(c, OP_NAT_METHOD, cls->line);
    emitShort(c, id, cls->line);
    emitShort(c, native, cls->line);
}

static void compileMethods(Compiler* c, Stmt* cls) {
    vecForeach(Stmt(**it), cls->as.classDecl.methods) {
        Stmt* method = *it;
        switch(method->type) {
        case FUNCDECL:
            compileMethod(c, cls, method);
            break;
        case NATIVEDECL:
            compileNativeMethod(c, cls, method);
            break;
        default:
            UNREACHABLE();
            break;
        }
    }
}

static void compileClass(Compiler* c, Stmt* s) {
    declareVar(c, &s->as.classDecl.id, s->line);

    bool isSubClass = s->as.classDecl.sup != NULL;
    if(isSubClass) {
        compileExpr(c, s->as.classDecl.sup);
        emitBytecode(c, OP_NEW_SUBCLASS, s->line);
    } else {
        emitBytecode(c, OP_NEW_CLASS, s->line);
    }

    emitShort(c, identifierConst(c, &s->as.classDecl.id, s->line), s->line);
    compileMethods(c, s);

    defineVar(c, &s->as.classDecl.id, s->line);
}

static void compileStatement(Compiler* c, Stmt* s) {
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
        compileStatements(c, &s->as.blockStmt.stmts);
        exitScope(c);
        break;
    case RETURN_STMT:
        compileReturnStatement(c, s);
        break;
    case IMPORT:
        compileImportStatement(c, s);
        break;
    case TRY_STMT:
        compileTryExcept(c, s);
        break;
    case RAISE_STMT:
        compileRaiseStmt(c, s);
        break;
    case WITH_STMT:
        compileWithStatement(c, s);
        break;
    case CONTINUE_STMT:
    case BREAK_STMT:
        compileLoopExitStmt(c, s);
        break;
    case EXPR:
        compileExpr(c, s->as.exprStmt);
        emitBytecode(c, OP_POP, 0);
        break;
    case VARDECL:
        compileVarDecl(c, s);
        break;
    case FUNCDECL:
        declareVar(c, &s->as.funcDecl.id, s->line);
        compileFunction(c, s);
        defineVar(c, &s->as.funcDecl.id, s->line);
        break;
    case NATIVEDECL:
        declareVar(c, &s->as.funcDecl.id, s->line);
        compileNative(c, s);
        defineVar(c, &s->as.funcDecl.id, s->line);
        break;
    case CLASSDECL:
        compileClass(c, s);
        break;
    case EXCEPT_STMT:
    default:
        UNREACHABLE();
        break;
    }
}

// -----------------------------------------------------------------------------
// API
// -----------------------------------------------------------------------------

ObjFunction* compile(JStarVM* vm, const char* filename, ObjModule* module, Stmt* ast) {
    Compiler c;
    initCompiler(&c, vm, filename, NULL, TYPE_FUNC, ast);
    ObjFunction* func = function(&c, module, ast);
    endCompiler(&c);
    return c.hadError ? NULL : func;
}

void reachCompilerRoots(JStarVM* vm, Compiler* c) {
    while(c != NULL) {
        reachObject(vm, (Obj*)c->func);
        c = c->prev;
    }
}
