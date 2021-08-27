#include "compiler.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "code.h"
#include "const.h"
#include "gc.h"
#include "jstar.h"
#include "opcode.h"
#include "parse/lex.h"
#include "parse/vector.h"
#include "profiler.h"
#include "util.h"
#include "value.h"
#include "vm.h"

// In case of a direct assignment of the form:
//   a, b, ..., c = ...
// Where the right hand side is an unpackable object (i.e. Tuple or List), we
// can omit its creation and assign directly the elements to the variables.
// We call this type of unpack assignement a 'const unpack'
#define IS_CONST_UNPACK(type) ((type) == JSR_ARRAY || (type) == JSR_TUPLE)

// Marker values used in the bytecode during the compilation of loop breaking statements.
// When we finish the compilation of a loop, and thus we know the addresses of its start
// and end, we replace these with jump offsets
#define CONTINUE_MARK 1
#define BREAK_MARK    2

typedef struct Variable {
    enum { VAR_LOCAL, VAR_GLOBAL, VAR_ERR } scope;
    union {
        struct {
            int index;
        } local;
        struct {
            JStarIdentifier id;
        } global;
    } as;
} Variable;

typedef struct LocalVar {
    JStarIdentifier id;
    bool isUpvalue;
    int depth;
} LocalVar;

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
    int numHandlers;
    struct TryExcept* next;
} TryExcept;

typedef enum FuncType {
    TYPE_FUNC,
    TYPE_METHOD,
    TYPE_CTOR,
} FuncType;

struct Compiler {
    JStarVM* vm;
    JStarBuffer stringBuf;

    const char* file;

    int depth;
    Compiler* prev;

    Loop* loops;

    FuncType type;
    ObjFunction* func;
    JStarStmt* ast;

    uint8_t localsCount;
    LocalVar locals[MAX_LOCALS];
    Upvalue upvalues[MAX_LOCALS];

    int tryDepth;
    TryExcept* tryBlocks;

    bool hadError;
};

static void initCompiler(Compiler* c, JStarVM* vm, const char* file, Compiler* prev, FuncType type,
                         JStarStmt* ast) {
    c->vm = vm;
    c->file = file;
    c->prev = prev;
    c->type = type;
    c->func = NULL;
    c->ast = ast;
    c->depth = 0;
    c->localsCount = 0;
    c->loops = NULL;
    c->tryDepth = 0;
    c->tryBlocks = NULL;
    c->hadError = false;
    jsrBufferInit(vm, &c->stringBuf);
    vm->currCompiler = c;
}

static void endCompiler(Compiler* c) {
    jsrBufferFree(&c->stringBuf);
    if(c->prev != NULL) c->prev->hadError |= c->hadError;
    c->vm->currCompiler = c->prev;
}

// -----------------------------------------------------------------------------
// UTILITY FUNCTIONS
// -----------------------------------------------------------------------------

static void error(Compiler* c, int line, const char* format, ...) {
    JStarVM* vm = c->vm;
    c->hadError = true;

    if(vm->errorCallback) {
        JStarBuffer error;
        jsrBufferInitCapacity(c->vm, &error, strlen(format) * 2);

        va_list args;
        va_start(args, format);
        jsrBufferAppendvf(&error, format, args);
        va_end(args);

        vm->errorCallback(vm, JSR_COMPILE_ERR, c->file, line, error.data);

        jsrBufferFree(&error);
    }
}

static size_t emitBytecode(Compiler* c, uint8_t b, int line) {
    if(line == 0 && c->func->code.lineSize > 0) {
        line = c->func->code.lines[c->func->code.lineSize - 1];
    }
    return writeByte(&c->func->code, b, line);
}

static size_t emitShort(Compiler* c, uint16_t s, int line) {
    size_t addr = emitBytecode(c, (s >> 8) & 0xff, line);
    emitBytecode(c, s & 0xff, line);
    return addr;
}

static size_t getCurrentAddr(Compiler* c) {
    return c->func->code.size;
}

static bool inGlobalScope(Compiler* c) {
    return c->depth == 0;
}

static void discardLocal(Compiler* c, LocalVar* local) {
    if(local->isUpvalue) {
        emitBytecode(c, OP_CLOSE_UPVALUE, 0);
    } else {
        emitBytecode(c, OP_POP, 0);
    }
}

static void enterScope(Compiler* c) {
    c->depth++;
}

static int discardScopes(Compiler* c, int depth) {
    int localsCount = c->localsCount;

    int toPop = 0;
    while(localsCount > 0 && c->locals[localsCount - 1].depth > depth) {
        toPop++, localsCount--;
    }

    if(toPop > 1) {
        emitBytecode(c, OP_POPN, 0);
        emitBytecode(c, toPop, 0);
    } else if(toPop == 1) {
        discardLocal(c, &c->locals[localsCount]);
    }

    return localsCount;
}

static void exitScope(Compiler* c) {
    c->localsCount = discardScopes(c, --c->depth);
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
        error(c, line, "Too many constants in function %s", c->func->proto.name->data);
        return 0;
    }
    return (uint16_t)index;
}

static JStarIdentifier createIdentifier(const char* name) {
    return (JStarIdentifier){strlen(name), name};
}

static uint16_t stringConst(Compiler* c, const char* str, size_t length, int line) {
    ObjString* idStr = copyString(c->vm, str, length);
    return createConst(c, OBJ_VAL(idStr), line);
}

static uint16_t identifierConst(Compiler* c, JStarIdentifier* id, int line) {
    return stringConst(c, id->name, id->length, line);
}

static int addLocal(Compiler* c, JStarIdentifier* id, int line) {
    if(c->localsCount == MAX_LOCALS) {
        error(c, line, "Too many local variables in function %s", c->func->proto.name->data);
        return -1;
    }

    LocalVar* local = &c->locals[c->localsCount];
    local->isUpvalue = false;
    local->depth = -1;
    local->id = *id;
    return c->localsCount++;
}

static int resolveVariable(Compiler* c, JStarIdentifier* id, int line) {
    for(int i = c->localsCount - 1; i >= 0; i--) {
        LocalVar* local = &c->locals[i];
        if(jsrIdentifierEq(&local->id, id)) {
            if(local->depth == -1) {
                error(c, line, "Cannot read local variable `%.*s` in its own initializer",
                      local->id.length, local->id.name);
                return 0;
            }
            return i;
        }
    }
    return -1;
}

static int addUpvalue(Compiler* c, uint8_t index, bool local, int line) {
    uint8_t upvalueCount = c->func->upvalueCount;
    for(uint8_t i = 0; i < upvalueCount; i++) {
        Upvalue* upval = &c->upvalues[i];
        if(upval->index == index && upval->isLocal == local) {
            return i;
        }
    }

    if(c->func->upvalueCount == MAX_LOCALS) {
        error(c, line, "Too many upvalues in function %s", c->func->proto.name->data);
        return -1;
    }

    c->upvalues[c->func->upvalueCount].isLocal = local;
    c->upvalues[c->func->upvalueCount].index = index;
    return c->func->upvalueCount++;
}

static int resolveUpvalue(Compiler* c, JStarIdentifier* id, int line) {
    if(c->prev == NULL) {
        return -1;
    }

    int i = resolveVariable(c->prev, id, line);
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

static Variable declareVar(Compiler* c, JStarIdentifier* id, bool forceLocal, int line) {
    // Global variables need not be declared
    if(inGlobalScope(c) && !forceLocal) {
        Variable var;
        var.scope = VAR_GLOBAL;
        var.as.global.id = *id;
        return var;
    }

    if(!inGlobalScope(c) && forceLocal) {
        error(c, line, "static declaration can only appear in global scope");
        return (Variable){VAR_ERR, {{0}}};
    }

    for(int i = c->localsCount - 1; i >= 0; i--) {
        if(c->locals[i].depth != -1 && c->locals[i].depth < c->depth) break;
        if(jsrIdentifierEq(&c->locals[i].id, id)) {
            error(c, line, "Variable `%.*s` already declared", id->length, id->name);
            return (Variable){VAR_ERR, {{0}}};
        }
    }

    int index = addLocal(c, id, line);
    if(index == -1) {
        return (Variable){VAR_ERR, {{0}}};
    }

    Variable var;
    var.scope = VAR_LOCAL;
    var.as.local.index = index;
    return var;
}

static void markInitialized(Compiler* c, int idx) {
    c->locals[idx].depth = c->depth;
}

static void defineVar(Compiler* c, Variable* var, int line) {
    switch(var->scope) {
    case VAR_GLOBAL:
        emitBytecode(c, OP_DEFINE_GLOBAL, line);
        emitShort(c, identifierConst(c, &var->as.global.id, line), line);
        break;
    case VAR_LOCAL:
        markInitialized(c, var->as.local.index);
        break;
    case VAR_ERR:
        break;
    }
}

static void assertJumpOpcode(Opcode op) {
    ASSERT((op == OP_JUMP || op == OP_JUMPT || op == OP_JUMPF || op == OP_FOR_NEXT ||
            op == OP_SETUP_EXCEPT || op == OP_SETUP_ENSURE),
           "Not a jump opcode");
}

static size_t emitJumpTo(Compiler* c, Opcode jmpOp, size_t target, int line) {
    assertJumpOpcode(jmpOp);
    
    int32_t offset = target - (getCurrentAddr(c) + opcodeArgsNumber(jmpOp) + 1);
    if(offset > INT16_MAX || offset < INT16_MIN) {
        error(c, line, "Too much code to jump over");
    }

    size_t jmpAddr = emitBytecode(c, jmpOp, line);
    emitShort(c, (uint16_t)offset, line);
    return jmpAddr;
}

static void setJumpTo(Compiler* c, size_t jumpAddr, size_t target, int line) {
    Code* code = &c->func->code;
    Opcode jmpOp = code->bytecode[jumpAddr];
    assertJumpOpcode(jmpOp);

    int32_t offset = target - (jumpAddr + opcodeArgsNumber(jmpOp) + 1);
    if(offset > INT16_MAX || offset < INT16_MIN) {
        error(c, line, "Too much code to jump over");
    }

    code->bytecode[jumpAddr + 1] = ((uint16_t)offset >> 8) & 0xff;
    code->bytecode[jumpAddr + 2] = ((uint16_t)offset) & 0xff;
}

static void startLoop(Compiler* c, Loop* loop) {
    loop->depth = c->depth;
    loop->start = getCurrentAddr(c);
    loop->next = c->loops;
    c->loops = loop;
}

static void patchLoopExitStmts(Compiler* c, size_t start, size_t contAddr, size_t brkAddr) {
    for(size_t i = start; i < getCurrentAddr(c); i++) {
        Opcode op = c->func->code.bytecode[i];
        if(op == OP_END) {
            c->func->code.bytecode[i] = OP_JUMP;

            // Patch jump with correct offset to break loop
            int mark = c->func->code.bytecode[i + 1];
            ASSERT(mark == CONTINUE_MARK || mark == BREAK_MARK, "Unknown loop breaking marker");
            setJumpTo(c, i, mark == CONTINUE_MARK ? contAddr : brkAddr, 0);

            i += opcodeArgsNumber(OP_JUMP);
        } else {
            i += opcodeArgsNumber(op);
        }
    }
}

static void endLoop(Compiler* c) {
    patchLoopExitStmts(c, c->loops->start, c->loops->start, getCurrentAddr(c));
    c->loops = c->loops->next;
}

static void emitMethodCall(Compiler* c, const char* name, int args) {
    ASSERT(args <= MAX_INLINE_ARGS, "Too many arguments for inline call");
    JStarIdentifier meth = createIdentifier(name);
    emitBytecode(c, OP_INVOKE_0 + args, 0);
    emitShort(c, identifierConst(c, &meth, 0), 0);
}

static void enterTryBlock(Compiler* c, TryExcept* exc, int numHandlers, int line) {
    exc->depth = c->depth;
    exc->numHandlers = numHandlers;
    exc->next = c->tryBlocks;
    c->tryBlocks = exc;
    c->tryDepth += numHandlers;

    if(c->tryDepth > MAX_TRY_DEPTH) {
        error(c, line, "Exceeded max number of nested exception handlers: max %d, got %d",
              MAX_TRY_DEPTH, c->tryDepth);
    }
}

static void exitTryBlock(Compiler* c) {
    c->tryDepth -= c->tryBlocks->numHandlers;
    c->tryBlocks = c->tryBlocks->next;
}

static ObjString* readString(Compiler* c, JStarExpr* e) {
    JStarBuffer* sb = &c->stringBuf;
    jsrBufferClear(sb);

    const char* str = e->as.string.str;
    size_t length = e->as.string.length;

    const int numEscapes = 11;
    const char* escaped = "\0\a\b\f\n\r\t\v\\\"'";
    const char* unescaped = "0abfnrtv\\\"'";

    for(size_t i = 0; i < length; i++) {
        if(str[i] == '\\') {
            int j = 0;
            for(j = 0; j < numEscapes; j++) {
                if(str[i + 1] == unescaped[j]) {
                    jsrBufferAppendChar(sb, escaped[j]);
                    i++;
                    break;
                }
            }
            if(j == numEscapes) {
                error(c, e->line, "Invalid escape character `%c`", str[i + 1]);
            }
        } else {
            jsrBufferAppendChar(sb, str[i]);
        }
    }

    return copyString(c->vm, sb->data, sb->size);
}

static void addFunctionDefaults(Compiler* c, Prototype* proto, Vector* defaultArgs) {
    int i = 0;
    vecForeach(JStarExpr** it, *defaultArgs) {
        JStarExpr* e = *it;
        switch(e->type) {
        case JSR_NUMBER:
            proto->defaults[i++] = NUM_VAL(e->as.num);
            break;
        case JSR_BOOL:
            proto->defaults[i++] = BOOL_VAL(e->as.boolean);
            break;
        case JSR_STRING:
            proto->defaults[i++] = OBJ_VAL(readString(c, e));
            break;
        case JSR_NULL:
            proto->defaults[i++] = NULL_VAL;
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

static void compileExpr(Compiler* c, JStarExpr* e);

static void compileBinaryExpr(Compiler* c, JStarExpr* e) {
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
    case TOK_AMPER:
        emitBytecode(c, OP_BAND, e->line);
        break;
    case TOK_PIPE:
        emitBytecode(c, OP_BOR, e->line);
        break;
    case TOK_TILDE:
        emitBytecode(c, OP_XOR, e->line);
        break;
    case TOK_LSHIFT:
        emitBytecode(c, OP_LSHIFT, e->line);
        break;
    case TOK_RSHIFT:
        emitBytecode(c, OP_RSHIFT, e->line);
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

static void compileLogicExpr(Compiler* c, JStarExpr* e) {
    compileExpr(c, e->as.binary.left);
    emitBytecode(c, OP_DUP, e->line);

    Opcode jmpOp = e->as.binary.op == TOK_AND ? OP_JUMPF : OP_JUMPT;
    size_t shortCircuit = emitBytecode(c, jmpOp, 0);
    emitShort(c, 0, 0);

    emitBytecode(c, OP_POP, e->line);
    compileExpr(c, e->as.binary.right);

    setJumpTo(c, shortCircuit, getCurrentAddr(c), e->line);
}

static void compileUnaryExpr(Compiler* c, JStarExpr* e) {
    compileExpr(c, e->as.unary.operand);
    switch(e->as.unary.op) {
    case TOK_MINUS:
        emitBytecode(c, OP_NEG, e->line);
        break;
    case TOK_BANG:
        emitBytecode(c, OP_NOT, e->line);
        break;
    case TOK_TILDE:
        emitBytecode(c, OP_INVERT, e->line);
        break;
    case TOK_HASH:
        emitMethodCall(c, "__len__", 0);
        break;
    case TOK_HASH_HASH:
        emitMethodCall(c, "__string__", 0);
        break;
    default:
        UNREACHABLE();
        break;
    }
}

static void compileTernaryExpr(Compiler* c, JStarExpr* e) {
    compileExpr(c, e->as.ternary.cond);

    size_t falseJmp = emitBytecode(c, OP_JUMPF, e->line);
    emitShort(c, 0, 0);

    compileExpr(c, e->as.ternary.thenExpr);
    size_t exitJmp = emitBytecode(c, OP_JUMP, e->line);
    emitShort(c, 0, 0);

    setJumpTo(c, falseJmp, getCurrentAddr(c), e->line);
    compileExpr(c, e->as.ternary.elseExpr);

    setJumpTo(c, exitJmp, getCurrentAddr(c), e->line);
}

static void compileVariable(Compiler* c, JStarIdentifier* id, bool set, int line) {
    int idx = resolveVariable(c, id, line);
    if(idx != -1) {
        if(set) {
            emitBytecode(c, OP_SET_LOCAL, line);
        } else {
            emitBytecode(c, OP_GET_LOCAL, line);
        }
        emitBytecode(c, idx, line);
    } else if((idx = resolveUpvalue(c, id, line)) != -1) {
        if(set) {
            emitBytecode(c, OP_SET_UPVALUE, line);
        } else {
            emitBytecode(c, OP_GET_UPVALUE, line);
        }
        emitBytecode(c, idx, line);
    } else {
        if(set) {
            emitBytecode(c, OP_SET_GLOBAL, line);
        } else {
            emitBytecode(c, OP_GET_GLOBAL, line);
        }
        emitShort(c, identifierConst(c, id, line), line);
    }
}

static void compileFunction(Compiler* c, JStarStmt* s);

static void compileFunLiteral(Compiler* c, JStarExpr* e, JStarIdentifier* name) {
    JStarStmt* func = e->as.funLit.func;
    if(name == NULL) {
        char funcName[sizeof(ANON_STR) + STRLEN_FOR_INT(int) + 1];
        sprintf(funcName, ANON_STR "%d", func->line);

        func->as.funcDecl.id = createIdentifier(funcName);
        compileFunction(c, func);
    } else {
        func->as.funcDecl.id = *name;
        compileFunction(c, func);
    }
}

static void compileLval(Compiler* c, JStarExpr* e) {
    switch(e->type) {
    case JSR_VAR:
        compileVariable(c, &e->as.var.id, true, e->line);
        break;
    case JSR_ACCESS: {
        compileExpr(c, e->as.access.left);
        emitBytecode(c, OP_SET_FIELD, e->line);
        emitShort(c, identifierConst(c, &e->as.access.id, e->line), e->line);
        break;
    }
    case JSR_ARR_ACCESS: {
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

// The `boundName` argument is the name of the variable to which we are assigning to.
// In case of a function literal we use it to give the function a meaningful name, instead
// of just using the default name for anonymous functions (that is: `anon:<line_number>`)
static void compileRval(Compiler* c, JStarExpr* e, JStarIdentifier* boundName) {
    if(e->type == JSR_FUNC_LIT) {
        compileFunLiteral(c, e, boundName);
    } else {
        compileExpr(c, e);
    }
}

static void compileConstUnpackLst(Compiler* c, JStarExpr* exprs, int lvals, Vector* names) {
    if(vecSize(&exprs->as.list) < (size_t)lvals) {
        error(c, exprs->line, "Too few values to unpack: expected %d, got %zu", lvals, 
              vecSize(&exprs->as.list));
    }

    int i = 0;
    vecForeach(JStarExpr** it, exprs->as.list) {
        JStarIdentifier* name = NULL;
        if(names && i < lvals) name = vecGet(names, i);
        compileRval(c, *it, name);
        
        if(++i > lvals) emitBytecode(c, OP_POP, 0);
    }
}

static JStarExpr* getUnpackableExprs(JStarExpr* unpackable) {
    switch(unpackable->type) {
    case JSR_ARRAY:
        return unpackable->as.array.exprs;
    case JSR_TUPLE:
        return unpackable->as.tuple.exprs;
    default:
        UNREACHABLE();
        return NULL;
    }
}

// Compile an unpack assignment of the form: a, b, ..., z = ...
static void compileUnpackAssign(Compiler* c, JStarExpr* e) {
    JStarExpr* lvals = e->as.assign.lval->as.tuple.exprs;
    size_t lvalCount = vecSize(&lvals->as.list);

    if(lvalCount >= UINT8_MAX) {
        error(c, e->line, "Exceeded max number of unpack assignment: %d", UINT8_MAX);
    }

    JStarExpr* rval = e->as.assign.rval;
    if(IS_CONST_UNPACK(rval->type)) {
        JStarExpr* exprs = getUnpackableExprs(rval);
        compileConstUnpackLst(c, exprs, lvalCount, NULL);
    } else {
        compileRval(c, rval, NULL);
        emitBytecode(c, OP_UNPACK, e->line);
        emitBytecode(c, (uint8_t)lvalCount, e->line);
    }

    // compile lvals in reverse order in order to assign
    // correct values to variables in case of a const unpack
    for(int n = lvalCount - 1; n >= 0; n--) {
        JStarExpr* lval = vecGet(&lvals->as.list, n);
        compileLval(c, lval);
        if(n != 0) emitBytecode(c, OP_POP, e->line);
    }
}

static void compileAssignExpr(Compiler* c, JStarExpr* e) {
    switch(e->as.assign.lval->type) {
    case JSR_VAR: {
        JStarIdentifier* name = &e->as.assign.lval->as.var.id;
        compileRval(c, e->as.assign.rval, name);
        compileLval(c, e->as.assign.lval);
        break;
    }
    case JSR_ACCESS: {
        JStarIdentifier* name = &e->as.assign.lval->as.access.id;
        compileRval(c, e->as.assign.rval, name);
        compileLval(c, e->as.assign.lval);
        break;
    }
    case JSR_ARR_ACCESS: {
        compileRval(c, e->as.assign.rval, NULL);
        compileLval(c, e->as.assign.lval);
        break;
    }
    case JSR_TUPLE: {
        compileUnpackAssign(c, e);
        break;
    }
    default:
        UNREACHABLE();
        break;
    }
}

static void compileCompundAssign(Compiler* c, JStarExpr* e) {
    JStarTokType op = e->as.compound.op;
    JStarExpr* l = e->as.compound.lval;
    JStarExpr* r = e->as.compound.rval;

    // expand compound assignement (e.g. a op= b -> a = a op b)
    JStarExpr binary = {e->line, JSR_BINARY, .as = {.binary = {op, l, r}}};
    JStarExpr assignment = {e->line, JSR_ASSIGN, .as = {.assign = {l, &binary}}};

    // compile as a normal assignment
    compileAssignExpr(c, &assignment);
}

static void finishCall(Compiler* c, Opcode callCode, Opcode callInline, Opcode callUnpack,
                       JStarExpr* args, bool isUnpack) {
    vecForeach(JStarExpr** it, args->as.list) {
        compileExpr(c, *it);
    }

    size_t argsCount = vecSize(&args->as.list);
    if(argsCount >= UINT8_MAX) {
        error(c, args->line, "Exceeded maximum number of arguments (%d) for function %s",
              (int)UINT8_MAX, c->func->proto.name->data);
    }

    if(isUnpack) {
        emitBytecode(c, callUnpack, args->line);
        emitBytecode(c, argsCount, args->line);
    } else if(argsCount <= MAX_INLINE_ARGS) {
        emitBytecode(c, callInline + argsCount, args->line);
    } else {
        emitBytecode(c, callCode, args->line);
        emitBytecode(c, argsCount, args->line);
    }
}

static void compileCallExpr(Compiler* c, JStarExpr* e) {
    Opcode callCode = OP_CALL;
    Opcode callInline = OP_CALL_0;
    Opcode callUnpack = OP_CALL_UNPACK;

    JStarExpr* callee = e->as.call.callee;
    bool isMethod = callee->type == JSR_ACCESS;

    if(isMethod) {
        callCode = OP_INVOKE;
        callInline = OP_INVOKE_0;
        callUnpack = OP_INVOKE_UNPACK;
        compileExpr(c, callee->as.access.left);
    } else {
        compileExpr(c, callee);
    }

    finishCall(c, callCode, callInline, callUnpack, e->as.call.args, e->as.call.unpackArg);

    if(isMethod) {
        emitShort(c, identifierConst(c, &callee->as.access.id, e->line), e->line);
    }
}

static void compileSuper(Compiler* c, JStarExpr* e) {
    if(c->type != TYPE_METHOD && c->type != TYPE_CTOR) {
        error(c, e->line, "Can only use `super` in method call");
        return;
    }

    emitBytecode(c, OP_GET_LOCAL, e->line);
    emitBytecode(c, 0, e->line);

    uint16_t nameConst;
    if(e->as.sup.name.name != NULL) {
        nameConst = identifierConst(c, &e->as.sup.name, e->line);
    } else {
        nameConst = identifierConst(c, &c->ast->as.funcDecl.id, e->line);
    }

    if(e->as.sup.args != NULL) {
        finishCall(c, OP_SUPER, OP_SUPER_0, OP_SUPER_UNPACK, e->as.sup.args, e->as.sup.unpackArg);
        emitShort(c, nameConst, e->line);
    } else {
        emitBytecode(c, OP_SUPER_BIND, e->line);
        emitShort(c, nameConst, e->line);
    }
}

static void compileAccessExpression(Compiler* c, JStarExpr* e) {
    compileExpr(c, e->as.access.left);
    emitBytecode(c, OP_GET_FIELD, e->line);
    emitShort(c, identifierConst(c, &e->as.access.id, e->line), e->line);
}

static void compileArraryAccExpression(Compiler* c, JStarExpr* e) {
    compileExpr(c, e->as.arrayAccess.left);
    compileExpr(c, e->as.arrayAccess.index);
    emitBytecode(c, OP_SUBSCR_GET, e->line);
}

static void compilePowExpr(Compiler* c, JStarExpr* e) {
    compileExpr(c, e->as.pow.base);
    compileExpr(c, e->as.pow.exp);
    emitBytecode(c, OP_POW, e->line);
}

static void CompileListLit(Compiler* c, JStarExpr* e) {
    emitBytecode(c, OP_NEW_LIST, e->line);
    vecForeach(JStarExpr** it, e->as.array.exprs->as.list) {
        compileExpr(c, *it);
        emitBytecode(c, OP_APPEND_LIST, e->line);
    }
}

static void compileTupleLit(Compiler* c, JStarExpr* e) {
    vecForeach(JStarExpr** it, e->as.tuple.exprs->as.list) {
        compileExpr(c, *it);
    }

    size_t tupleSize = vecSize(&e->as.tuple.exprs->as.list);
    if(tupleSize >= UINT8_MAX) error(c, e->line, "Too many elements in Tuple literal");
    emitBytecode(c, OP_NEW_TUPLE, e->line);
    emitBytecode(c, tupleSize, e->line);
}

static void compileTableLit(Compiler* c, JStarExpr* e) {
    emitBytecode(c, OP_NEW_TABLE, e->line);

    JStarExpr* keyVals = e->as.table.keyVals;
    for(JStarExpr** it = vecBegin(&keyVals->as.list); it != vecEnd(&keyVals->as.list); it += 2) {
        JStarExpr* key = *it;
        JStarExpr* val = *(it + 1);

        emitBytecode(c, OP_DUP, e->line);
        compileExpr(c, key);
        compileExpr(c, val);
        
        emitMethodCall(c, "__set__", 2);
        emitBytecode(c, OP_POP, e->line);
    }
}

static void emitValueConst(Compiler* c, Value val, int line) {
    emitBytecode(c, OP_GET_CONST, line);
    emitShort(c, createConst(c, val, line), line);
}

static void compileExpr(Compiler* c, JStarExpr* e) {
    switch(e->type) {
    case JSR_BINARY:
        if(e->as.binary.op == TOK_AND || e->as.binary.op == TOK_OR) {
            compileLogicExpr(c, e);
        } else {
            compileBinaryExpr(c, e);
        }
        break;
    case JSR_ASSIGN:
        compileAssignExpr(c, e);
        break;
    case JSR_COMPUND_ASS:
        compileCompundAssign(c, e);
        break;
    case JSR_UNARY:
        compileUnaryExpr(c, e);
        break;
    case JSR_TERNARY:
        compileTernaryExpr(c, e);
        break;
    case JSR_CALL:
        compileCallExpr(c, e);
        break;
    case JSR_ACCESS:
        compileAccessExpression(c, e);
        break;
    case JSR_ARR_ACCESS:
        compileArraryAccExpression(c, e);
        break;
    case JSR_POWER:
        compilePowExpr(c, e);
        break;
    case JSR_NUMBER:
        emitValueConst(c, NUM_VAL(e->as.num), e->line);
        break;
    case JSR_BOOL:
        emitValueConst(c, BOOL_VAL(e->as.boolean), e->line);
        break;
    case JSR_STRING:
        emitValueConst(c, OBJ_VAL(readString(c, e)), e->line);
        break;
    case JSR_VAR:
        compileVariable(c, &e->as.var.id, false, e->line);
        break;
    case JSR_NULL:
        emitBytecode(c, OP_NULL, e->line);
        break;
    case JSR_ARRAY:
        CompileListLit(c, e);
        break;
    case JSR_TUPLE:
        compileTupleLit(c, e);
        break;
    case JSR_TABLE:
        compileTableLit(c, e);
        break;
    case JSR_SUPER:
        compileSuper(c, e);
        break;
    case JSR_FUNC_LIT:
        compileFunLiteral(c, e, NULL);
        break;
    case JSR_EXPR_LST:
        vecForeach(JStarExpr** it, e->as.list) {
            compileExpr(c, *it);
        }
        break;
    }
}

// -----------------------------------------------------------------------------
// STATEMENT COMPILE
// -----------------------------------------------------------------------------

static void compileStatement(Compiler* c, JStarStmt* s);

static void compileStatements(Compiler* c, Vector* stmts) {
    vecForeach(JStarStmt** it, *stmts) {
        compileStatement(c, *it);
    }
}

static void compileReturnStatement(Compiler* c, JStarStmt* s) {
    if(c->prev == NULL) {
        error(c, s->line, "Cannot use return outside a function");
    }
    if(c->type == TYPE_CTOR) {
        error(c, s->line, "Cannot use return in constructor");
    }

    if(s->as.returnStmt.e != NULL) {
        compileExpr(c, s->as.returnStmt.e);
    } else {
        emitBytecode(c, OP_NULL, s->line);
    }

    emitBytecode(c, OP_RETURN, s->line);
}

static void compileIfStatement(Compiler* c, JStarStmt* s) {
    compileExpr(c, s->as.ifStmt.cond);

    size_t falseJmp = emitBytecode(c, OP_JUMPF, 0);
    emitShort(c, 0, 0);

    compileStatement(c, s->as.ifStmt.thenStmt);

    size_t exitJmp = 0;
    if(s->as.ifStmt.elseStmt != NULL) {
        exitJmp = emitBytecode(c, OP_JUMP, 0);
        emitShort(c, 0, 0);
    }

    setJumpTo(c, falseJmp, getCurrentAddr(c), s->line);

    if(s->as.ifStmt.elseStmt != NULL) {
        compileStatement(c, s->as.ifStmt.elseStmt);
        setJumpTo(c, exitJmp, getCurrentAddr(c), s->line);
    }
}

static void compileForStatement(Compiler* c, JStarStmt* s) {
    enterScope(c);

    if(s->as.forStmt.init != NULL) {
        compileStatement(c, s->as.forStmt.init);
    }

    size_t firstJmp = 0;
    if(s->as.forStmt.act != NULL) {
        firstJmp = emitBytecode(c, OP_JUMP, 0);
        emitShort(c, 0, 0);
    }

    Loop l;
    startLoop(c, &l);

    if(s->as.forStmt.act != NULL) {
        compileExpr(c, s->as.forStmt.act);
        emitBytecode(c, OP_POP, 0);
        setJumpTo(c, firstJmp, getCurrentAddr(c), 0);
    }

    size_t exitJmp = 0;
    if(s->as.forStmt.cond != NULL) {
        compileExpr(c, s->as.forStmt.cond);
        exitJmp = emitBytecode(c, OP_JUMPF, 0);
        emitShort(c, 0, 0);
    }

    compileStatement(c, s->as.forStmt.body);
    emitJumpTo(c, OP_JUMP, l.start, s->line);

    if(s->as.forStmt.cond != NULL) {
        setJumpTo(c, exitJmp, getCurrentAddr(c), 0);
    }

    endLoop(c);
    exitScope(c);
}

/*
 * for var i in iterable
 *     ...
 * end
 *
 * begin
 *     var iter = null
 *     var expr = iterable
 *     while iter = expr.__iter__(iter)
 *         var i = expr.__next__(iter)
 *         ...
 *     end
 * end
 */
static void compileForEach(Compiler* c, JStarStmt* s) {
    enterScope(c);

    // Evaluate `iterable` once and store it in a local
    JStarIdentifier expr = createIdentifier(".expr");
    Variable exprVar = declareVar(c, &expr, false, s->as.forEach.iterable->line);
    defineVar(c, &exprVar, s->as.forEach.iterable->line);
    compileExpr(c, s->as.forEach.iterable);

    // Set the iterator variable with a name that it's not an identifier.
    JStarIdentifier iterator = createIdentifier(".iter");
    Variable iterVar = declareVar(c, &iterator, false, s->line);
    defineVar(c, &iterVar, s->line);
    emitBytecode(c, OP_NULL, 0);  // initialize `.iter` to null

    // FOR_PREP will cache the __iter__ and __next__ methods as locals
    emitBytecode(c, OP_FOR_PREP, 0);

    // Declare variables for cached methods
    JStarIdentifier iterMeth = createIdentifier(".__iter__");
    Variable iterMethVar = declareVar(c, &iterMeth, false, s->line);
    defineVar(c, &iterMethVar, s->line);

    JStarIdentifier nextMeth = createIdentifier(".__next__");
    Variable nextMethVar = declareVar(c, &nextMeth, false, s->line);
    defineVar(c, &nextMethVar, s->line);

    Loop l;
    startLoop(c, &l);

    emitBytecode(c, OP_FOR_ITER, s->line);
    size_t exitJmp = emitBytecode(c, OP_FOR_NEXT, 0);
    emitShort(c, 0, 0);

    JStarStmt* varDecl = s->as.forEach.var;
    enterScope(c);

    vecForeach(JStarIdentifier** id, varDecl->as.varDecl.ids) {
        Variable var = declareVar(c, *id, false, s->line);
        defineVar(c, &var, s->line);
    }

    uint8_t numDecls = vecSize(&varDecl->as.varDecl.ids);
    if(varDecl->as.varDecl.isUnpack) {
        emitBytecode(c, OP_UNPACK, s->line);
        emitBytecode(c, numDecls, s->line);
    }

    JStarStmt* body = s->as.forEach.body;
    compileStatements(c, &body->as.blockStmt.stmts);

    exitScope(c);

    emitJumpTo(c, OP_JUMP, l.start, s->line);
    setJumpTo(c, exitJmp, getCurrentAddr(c), s->line);

    endLoop(c);
    exitScope(c);
}

static void compileWhileStatement(Compiler* c, JStarStmt* s) {
    Loop l;
    startLoop(c, &l);

    compileExpr(c, s->as.whileStmt.cond);
    size_t exitJmp = emitBytecode(c, OP_JUMPF, 0);
    emitShort(c, 0, 0);

    compileStatement(c, s->as.whileStmt.body);

    emitJumpTo(c, OP_JUMP, l.start, s->line);
    setJumpTo(c, exitJmp, getCurrentAddr(c), s->line);

    endLoop(c);
}

static void compileImportStatement(Compiler* c, JStarStmt* s) {
    Vector* modules = &s->as.importStmt.modules;
    Vector* names = &s->as.importStmt.impNames;
    bool importFor = !vecEmpty(names);
    bool importAs = s->as.importStmt.as.name != NULL;

    JStarBuffer nameBuf;
    jsrBufferInit(c->vm, &nameBuf);

    Variable modVar;
    if(!importFor) {
        JStarIdentifier* name = importAs ? &s->as.importStmt.as : vecGet(modules, 0);
        modVar = declareVar(c, name, false, s->line);
    }

    vecForeach(JStarIdentifier** it, *modules) {
        JStarIdentifier* subMod = *it;
        jsrBufferAppend(&nameBuf, subMod->name, subMod->length);

        if(importAs && vecIsIterEnd(modules, it)) {
            emitBytecode(c, OP_IMPORT, s->line);
        } else if(it == vecBegin(modules) && !(importAs || importFor)) {
            emitBytecode(c, OP_IMPORT, s->line);
        } else {
            emitBytecode(c, OP_IMPORT_FROM, s->line);
        }

        emitShort(c, stringConst(c, nameBuf.data, nameBuf.size, s->line), s->line);
        emitBytecode(c, OP_POP, s->line);
        
        if(!vecIsIterEnd(modules, it)) jsrBufferAppendChar(&nameBuf, '.');
    }

    if(importFor) {
        vecForeach(JStarIdentifier** it, *names) {
            JStarIdentifier* name = *it;

            emitBytecode(c, OP_IMPORT_NAME, s->line);
            emitShort(c, stringConst(c, nameBuf.data, nameBuf.size, s->line), s->line);
            emitShort(c, identifierConst(c, name, s->line), s->line);

            Variable nameVar = declareVar(c, name, false, s->line);
            defineVar(c, &nameVar, s->line);
        }
    } else {
        defineVar(c, &modVar, s->line);
    }

    jsrBufferFree(&nameBuf);
}

static void compileExcepts(Compiler* c, Vector* excepts, size_t n) {
    JStarStmt* except = vecGet(excepts, n);

    JStarIdentifier exception = createIdentifier(".exception");
    compileVariable(c, &exception, false, except->line);

    compileExpr(c, except->as.excStmt.cls);
    emitBytecode(c, OP_IS, 0);

    size_t falseJmp = emitBytecode(c, OP_JUMPF, 0);
    emitShort(c, 0, 0);

    enterScope(c);

    compileVariable(c, &exception, false, except->line);
    Variable excVar = declareVar(c, &except->as.excStmt.var, false, except->line);
    defineVar(c, &excVar, except->line);

    JStarStmt* body = except->as.excStmt.block;
    compileStatements(c, &body->as.blockStmt.stmts);

    // Set the exception cause to `null` to signal that the exception has been handled
    JStarIdentifier cause = createIdentifier(".cause");
    emitBytecode(c, OP_NULL, except->line);
    compileVariable(c, &cause, true, except->line);
    emitBytecode(c, OP_POP, except->line);

    exitScope(c);

    size_t exitJmp = 0;
    if(n < vecSize(excepts) - 1) {
        exitJmp = emitBytecode(c, OP_JUMP, 0);
        emitShort(c, 0, 0);
    }

    setJumpTo(c, falseJmp, getCurrentAddr(c), except->line);

    if(n < vecSize(excepts) - 1) {
        compileExcepts(c, excepts, n + 1);
        setJumpTo(c, exitJmp, getCurrentAddr(c), except->line);
    }
}

static void compileTryExcept(Compiler* c, JStarStmt* s) {
    bool hasExcepts = !vecEmpty(&s->as.tryStmt.excs);
    bool hasEnsure = s->as.tryStmt.ensure != NULL;
    int numHandlers = (hasExcepts ? 1 : 0) + (hasEnsure ? 1 : 0);

    TryExcept tryBlock;
    enterTryBlock(c, &tryBlock, numHandlers, s->line);

    size_t ensSetup = 0, excSetup = 0;

    if(hasEnsure) {
        ensSetup = emitBytecode(c, OP_SETUP_ENSURE, s->line);
        emitShort(c, 0, 0);
    }

    if(hasExcepts) {
        excSetup = emitBytecode(c, OP_SETUP_EXCEPT, s->line);
        emitShort(c, 0, 0);
    }

    compileStatement(c, s->as.tryStmt.block);

    if(hasExcepts) {
        emitBytecode(c, OP_POP_HANDLER, s->line);
    }

    if(hasEnsure) {
        emitBytecode(c, OP_POP_HANDLER, s->line);
        // Reached end of try block during normal execution flow, set exception and unwind
        // cause to null to signal the ensure handler that no exception was raised
        emitBytecode(c, OP_NULL, s->line);
        emitBytecode(c, OP_NULL, s->line);
    }

    enterScope(c);

    JStarIdentifier exc = createIdentifier(".exception");
    Variable excVar = declareVar(c, &exc, false, 0);
    defineVar(c, &excVar, 0);

    JStarIdentifier cause = createIdentifier(".cause");
    Variable causeVar = declareVar(c, &cause, false, 0);
    defineVar(c, &causeVar, 0);

    if(hasExcepts) {
        size_t excJmp = emitBytecode(c, OP_JUMP, 0);
        emitShort(c, 0, 0);

        setJumpTo(c, excSetup, getCurrentAddr(c), s->line);
        compileExcepts(c, &s->as.tryStmt.excs, 0);

        if(hasEnsure) {
            emitBytecode(c, OP_POP_HANDLER, 0);
        } else {
            emitBytecode(c, OP_END_HANDLER, 0);
            exitScope(c);
        }

        setJumpTo(c, excJmp, getCurrentAddr(c), 0);
    }

    if(hasEnsure) {
        setJumpTo(c, ensSetup, getCurrentAddr(c), s->line);
        compileStatement(c, s->as.tryStmt.ensure);
        emitBytecode(c, OP_END_HANDLER, 0);
        exitScope(c);
    }

    exitTryBlock(c);
}

static void compileRaiseStmt(Compiler* c, JStarStmt* s) {
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
static void compileWithStatement(Compiler* c, JStarStmt* s) {
    enterScope(c);

    // var x
    emitBytecode(c, OP_NULL, s->line);
    Variable var = declareVar(c, &s->as.withStmt.var, false, s->line);
    defineVar(c, &var, s->line);

    // try
    TryExcept tryBlock;
    enterTryBlock(c, &tryBlock, 1, s->line);

    size_t ensSetup = emitBytecode(c, OP_SETUP_ENSURE, s->line);
    emitShort(c, 0, 0);

    // x = closable
    JStarExpr lval = {.line = s->line, .type = JSR_VAR, .as = {.var = {s->as.withStmt.var}}};
    JStarExpr assign = {.line = s->line,
                        .type = JSR_ASSIGN,
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

    JStarIdentifier exc = createIdentifier(".exception");
    Variable excVar = declareVar(c, &exc, false, 0);
    defineVar(c, &excVar, 0);

    JStarIdentifier cause = createIdentifier(".cause");
    Variable causeVar = declareVar(c, &cause, false, 0);
    defineVar(c, &causeVar, 0);

    setJumpTo(c, ensSetup, getCurrentAddr(c), s->line);

    // if x then x.close() end
    compileVariable(c, &s->as.withStmt.var, false, s->line);
    size_t falseJmp = emitBytecode(c, OP_JUMPF, s->line);
    emitShort(c, 0, 0);

    compileVariable(c, &s->as.withStmt.var, false, s->line);
    emitMethodCall(c, "close", 0);
    emitBytecode(c, OP_POP, s->line);

    setJumpTo(c, falseJmp, getCurrentAddr(c), s->line);

    emitBytecode(c, OP_END_HANDLER, 0);
    exitScope(c);

    exitTryBlock(c);
    exitScope(c);
}

static void compileLoopExitStmt(Compiler* c, JStarStmt* s) {
    bool isBreak = s->type == JSR_BREAK;

    if(c->loops == NULL) {
        error(c, s->line, "Cannot use %s outside loop", isBreak ? "break" : "continue");
        return;
    }

    if(c->tryDepth != 0 && c->tryBlocks->depth >= c->loops->depth) {
        error(c, s->line, "Cannot %s out of a try block", isBreak ? "break" : "continue");
    }

    discardScopes(c, c->loops->depth);

    // Emit place-holder instruction that will be patched at the end of loop compilation,
    // when we know the offset to emit for a break or continue jump
    emitBytecode(c, OP_END, s->line);
    emitBytecode(c, isBreak ? BREAK_MARK : CONTINUE_MARK, s->line);
    emitBytecode(c, 0, s->line);
}

static ObjFunction* function(Compiler* c, ObjModule* module, JStarStmt* s) {
    JStarIdentifier* name = &s->as.funcDecl.id;
    size_t defaults = vecSize(&s->as.funcDecl.defArgs);
    size_t arity = vecSize(&s->as.funcDecl.formalArgs);
    bool vararg = s->as.funcDecl.isVararg;

    c->func = newFunction(c->vm, module, arity, defaults, vararg);
    addFunctionDefaults(c, &c->func->proto, &s->as.funcDecl.defArgs);

    if(name->length != 0) {
        c->func->proto.name = copyString(c->vm, name->name, name->length);
    } else {
        c->func->proto.name = copyString(c->vm, "<main>", strlen("<main>"));
    }

    // add phony variable for function receiver (in the case of functions the
    // receiver is the function itself but it ins't accessible)
    JStarIdentifier id = createIdentifier("");
    addLocal(c, &id, s->line);

    vecForeach(JStarIdentifier** it, s->as.funcDecl.formalArgs) {
        Variable arg = declareVar(c, *it, false, s->line);
        defineVar(c, &arg, s->line);
    }

    if(s->as.funcDecl.isVararg) {
        JStarIdentifier args = createIdentifier("args");
        Variable vararg = declareVar(c, &args, false, s->line);
        defineVar(c, &vararg, s->line);
    }

    JStarStmt* body = s->as.funcDecl.body;
    compileStatements(c, &body->as.blockStmt.stmts);

    emitBytecode(c, OP_NULL, 0);
    emitBytecode(c, OP_RETURN, 0);

    return c->func;
}

static ObjString* createMethodName(Compiler* c, JStarIdentifier* clsId, JStarIdentifier* methId) {
    size_t length = clsId->length + methId->length + 1;
    ObjString* name = allocateString(c->vm, length);
    memcpy(name->data, clsId->name, clsId->length);
    name->data[clsId->length] = '.';
    memcpy(name->data + clsId->length + 1, methId->name, methId->length);
    return name;
}

static ObjFunction* method(Compiler* c, ObjModule* mod, JStarIdentifier* clsName, JStarStmt* s) {
    JStarIdentifier* name = &s->as.funcDecl.id;
    size_t defCount = vecSize(&s->as.funcDecl.defArgs);
    size_t arity = vecSize(&s->as.funcDecl.formalArgs);
    bool vararg = s->as.funcDecl.isVararg;

    c->func = newFunction(c->vm, mod, arity, defCount, vararg);
    addConstant(&c->func->code, NULL_VAL);  // This const will hold the superclass at runtime
    addFunctionDefaults(c, &c->func->proto, &s->as.funcDecl.defArgs);

    c->func->proto.name = createMethodName(c, clsName, name);

    // if in costructor change the type
    JStarIdentifier ctor = createIdentifier(CTOR_STR);
    if(jsrIdentifierEq(&s->as.funcDecl.id, &ctor)) {
        c->type = TYPE_CTOR;
    }

    // add `this` for method receiver (the object from which was called)
    JStarIdentifier thisId = createIdentifier(THIS_STR);
    Variable thisVar = declareVar(c, &thisId, false, s->line);
    defineVar(c, &thisVar, s->line);

    // define and declare arguments
    vecForeach(JStarIdentifier** it, s->as.funcDecl.formalArgs) {
        Variable arg = declareVar(c, *it, false, s->line);
        defineVar(c, &arg, s->line);
    }

    if(s->as.funcDecl.isVararg) {
        JStarIdentifier args = createIdentifier("args");
        Variable vararg = declareVar(c, &args, false, s->line);
        defineVar(c, &vararg, s->line);
    }

    JStarStmt* body = s->as.funcDecl.body;
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

static void compileFunction(Compiler* c, JStarStmt* s) {
    Compiler funCompiler;
    initCompiler(&funCompiler, c->vm, c->file, c, TYPE_FUNC, s);

    enterFunctionScope(&funCompiler);
    ObjFunction* func = function(&funCompiler, c->func->proto.module, s);
    exitFunctionScope(&funCompiler);

    emitBytecode(c, OP_CLOSURE, s->line);
    emitShort(c, createConst(c, OBJ_VAL(func), s->line), s->line);

    for(uint8_t i = 0; i < func->upvalueCount; i++) {
        emitBytecode(c, funCompiler.upvalues[i].isLocal ? 1 : 0, s->line);
        emitBytecode(c, funCompiler.upvalues[i].index, s->line);
    }

    endCompiler(&funCompiler);
}

static void compileNative(Compiler* c, JStarStmt* s) {
    JStarIdentifier* name = &s->as.nativeDecl.id;
    size_t defCount = vecSize(&s->as.nativeDecl.defArgs);
    size_t arity = vecSize(&s->as.nativeDecl.formalArgs);
    bool vararg = s->as.nativeDecl.isVararg;

    ObjNative* native = newNative(c->vm, c->func->proto.module, arity, defCount, vararg);
    push(c->vm, OBJ_VAL(native));

    addFunctionDefaults(c, &native->proto, &s->as.nativeDecl.defArgs);
    native->proto.name = copyString(c->vm, name->name, name->length);

    emitBytecode(c, OP_GET_CONST, s->line);
    emitShort(c, createConst(c, OBJ_VAL(native), s->line), s->line);

    emitBytecode(c, OP_NATIVE, s->line);
    emitShort(c, identifierConst(c, name, s->line), s->line);

    pop(c->vm);
}

static void compileMethod(Compiler* c, JStarStmt* cls, JStarStmt* m) {
    Compiler methodCompiler;
    initCompiler(&methodCompiler, c->vm, c->file, c, TYPE_METHOD, m);

    enterFunctionScope(&methodCompiler);
    ObjFunction* meth = method(&methodCompiler, c->func->proto.module, &cls->as.classDecl.id, m);
    exitFunctionScope(&methodCompiler);

    emitBytecode(c, OP_CLOSURE, m->line);
    emitShort(c, createConst(c, OBJ_VAL(meth), m->line), m->line);

    for(uint8_t i = 0; i < meth->upvalueCount; i++) {
        emitBytecode(c, methodCompiler.upvalues[i].isLocal ? 1 : 0, m->line);
        emitBytecode(c, methodCompiler.upvalues[i].index, m->line);
    }

    emitBytecode(c, OP_DEF_METHOD, cls->line);
    emitShort(c, identifierConst(c, &m->as.funcDecl.id, m->line), cls->line);
    endCompiler(&methodCompiler);
}

static void compileNativeMethod(Compiler* c, JStarStmt* cls, JStarStmt* m) {
    size_t defaults = vecSize(&m->as.nativeDecl.defArgs);
    size_t arity = vecSize(&m->as.nativeDecl.formalArgs);
    bool vararg = m->as.nativeDecl.isVararg;

    ObjNative* native = newNative(c->vm, c->func->proto.module, arity, defaults, vararg);
    push(c->vm, OBJ_VAL(native));

    addFunctionDefaults(c, &native->proto, &m->as.nativeDecl.defArgs);
    native->proto.name = createMethodName(c, &cls->as.classDecl.id, &m->as.funcDecl.id);

    emitBytecode(c, OP_NAT_METHOD, cls->line);
    emitShort(c, identifierConst(c, &m->as.nativeDecl.id, m->line), cls->line);
    emitShort(c, createConst(c, OBJ_VAL(native), cls->line), cls->line);

    pop(c->vm);
}

static void compileMethods(Compiler* c, JStarStmt* cls) {
    vecForeach(JStarStmt** it, cls->as.classDecl.methods) {
        JStarStmt* method = *it;
        switch(method->type) {
        case JSR_FUNCDECL:
            compileMethod(c, cls, method);
            break;
        case JSR_NATIVEDECL:
            compileNativeMethod(c, cls, method);
            break;
        default:
            UNREACHABLE();
            break;
        }
    }
}

static void compileVarDecl(Compiler* c, JStarStmt* s) {
    int varsCount = 0;
    Variable vars[MAX_LOCALS];
    vecForeach(JStarIdentifier** it, s->as.varDecl.ids) {
        Variable var = declareVar(c, *it, s->as.varDecl.isStatic, s->line);
        if(varsCount == MAX_LOCALS) break;
        vars[varsCount++] = var;
    }

    if(s->as.varDecl.init != NULL) {
        JStarExpr* init = s->as.varDecl.init;
        bool isUnpack = s->as.varDecl.isUnpack;

        if(isUnpack && IS_CONST_UNPACK(init->type)) {
            JStarExpr* exprs = getUnpackableExprs(init);
            compileConstUnpackLst(c, exprs, varsCount, &s->as.varDecl.ids);
        } else {
            compileRval(c, init, vecGet(&s->as.varDecl.ids, 0));
            if(isUnpack) {
                emitBytecode(c, OP_UNPACK, s->line);
                emitBytecode(c, (uint8_t)varsCount, s->line);
            }
        }
    } else {
        // Default initialize the variables to null
        for(int i = 0; i < varsCount; i++) {
            emitBytecode(c, OP_NULL, s->line);
        }
    }

    // define in reverse order in order to assign correct
    // values to variables in case of a const unpack
    for(int i = varsCount - 1; i >= 0; i--) {
        defineVar(c, &vars[i], s->line);
    }
}

static void compileClassDecl(Compiler* c, JStarStmt* s) {
    Variable clsVar = declareVar(c, &s->as.classDecl.id, s->as.classDecl.isStatic, s->line);
    // If local initialize the variable in order to permit the class to reference itself
    if(clsVar.scope == VAR_LOCAL) markInitialized(c, clsVar.as.local.index);

    if(s->as.classDecl.sup != NULL) {
        compileExpr(c, s->as.classDecl.sup);
        emitBytecode(c, OP_NEW_SUBCLASS, s->line);
    } else {
        emitBytecode(c, OP_NEW_CLASS, s->line);
    }

    emitShort(c, identifierConst(c, &s->as.classDecl.id, s->line), s->line);
    compileMethods(c, s);

    defineVar(c, &clsVar, s->line);
}

static void compileFunDecl(Compiler* c, JStarStmt* s) {
    Variable funVar = declareVar(c, &s->as.funcDecl.id, s->as.funcDecl.isStatic, s->line);
    // If local initialize the variable in order to permit the function to reference itself
    if(funVar.scope == VAR_LOCAL) markInitialized(c, funVar.as.local.index);
    compileFunction(c, s);
    defineVar(c, &funVar, s->line);
}

static void compileNativeDecl(Compiler* c, JStarStmt* s) {
    Variable natVar = declareVar(c, &s->as.funcDecl.id, s->as.nativeDecl.isStatic, s->line);
    compileNative(c, s);
    defineVar(c, &natVar, s->line);
}

static void compileStatement(Compiler* c, JStarStmt* s) {
    switch(s->type) {
    case JSR_IF:
        compileIfStatement(c, s);
        break;
    case JSR_FOR:
        compileForStatement(c, s);
        break;
    case JSR_FOREACH:
        compileForEach(c, s);
        break;
    case JSR_WHILE:
        compileWhileStatement(c, s);
        break;
    case JSR_BLOCK:
        enterScope(c);
        compileStatements(c, &s->as.blockStmt.stmts);
        exitScope(c);
        break;
    case JSR_RETURN:
        compileReturnStatement(c, s);
        break;
    case JSR_IMPORT:
        compileImportStatement(c, s);
        break;
    case JSR_TRY:
        compileTryExcept(c, s);
        break;
    case JSR_RAISE:
        compileRaiseStmt(c, s);
        break;
    case JSR_WITH:
        compileWithStatement(c, s);
        break;
    case JSR_CONTINUE:
    case JSR_BREAK:
        compileLoopExitStmt(c, s);
        break;
    case JSR_EXPR_STMT:
        compileExpr(c, s->as.exprStmt);
        emitBytecode(c, OP_POP, 0);
        break;
    case JSR_VARDECL:
        compileVarDecl(c, s);
        break;
    case JSR_CLASSDECL:
        compileClassDecl(c, s);
        break;
    case JSR_FUNCDECL:
        compileFunDecl(c, s);
        break;
    case JSR_NATIVEDECL:
        compileNativeDecl(c, s);
        break;
    case JSR_EXCEPT:
    default:
        UNREACHABLE();
        break;
    }
}

// -----------------------------------------------------------------------------
// API
// -----------------------------------------------------------------------------

ObjFunction* compile(JStarVM* vm, const char* filename, ObjModule* module, JStarStmt* ast) {
    PROFILE_FUNC()

    Compiler c;
    initCompiler(&c, vm, filename, NULL, TYPE_FUNC, ast);
    ObjFunction* func = function(&c, module, ast);
    endCompiler(&c);

    return c.hadError ? NULL : func;
}

void reachCompilerRoots(JStarVM* vm, Compiler* c) {
    PROFILE_FUNC()

    while(c != NULL) {
        reachObject(vm, (Obj*)c->func);
        c = c->prev;
    }
}
