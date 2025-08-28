#include "compiler.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "array.h"
#include "buffer.h"
#include "builtins/core/core.h"
#include "code.h"
#include "conf.h"
#include "gc.h"
#include "int_hashtable.h"
#include "jstar.h"
#include "jstar_limits.h"
#include "object.h"
#include "opcode.h"
#include "parse/ast.h"
#include "parse/lex.h"
#include "profile.h"
#include "util.h"
#include "value.h"
#include "vm.h"

// In case of a direct assignment of the form:
//   a, b, ..., c = ...
// Where the right hand side is an unpackable object (i.e. Tuple or List), we
// can omit its creation and assign directly the elements to the variables.
// We call this type of unpack assignement a 'const unpack'
#define IS_CONST_UNPACK(type) ((type) == JSR_LIST || (type) == JSR_TUPLE)

// Marker values used in the bytecode during the compilation of loop breaking statements.
// When we finish the compilation of a loop, and thus we know the addresses of its start
// and end, we replace these with jump offsets
#define CONTINUE_MARK 1
#define BREAK_MARK    2

// Max number of inline opcode arguments for a function call
#define MAX_INLINE_ARGS 10

// String constants
#define THIS_STR       "this"
#define ANON_FMT       "<anonymous>:%d:%d"
#define UNPACK_ARG_FMT "@unpack:%d"

typedef enum VariableScope {
    VAR_LOCAL,
    VAR_UPVALUE,
    VAR_GLOBAL,
    VAR_ERR,
} VariableScope;

typedef struct Variable {
    VariableScope scope;
    union {
        struct {
            int localIdx;
        } local;
        struct {
            int upvalueIdx;
        } upvalue;
        struct {
            JStarIdentifier id;
        } global;
    } as;
} Variable;

typedef struct Local {
    JStarLoc loc;
    JStarIdentifier id;
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
    struct Loop* parent;
} Loop;

typedef struct TryBlock {
    int depth;
    int numHandlers;
    struct TryBlock* parent;
} TryBlock;

typedef struct FwdRef {
    JStarIdentifier id;
    JStarLoc loc;
} FwdRef;

typedef struct {
    FwdRef* items;
    size_t count, capacity;
} FwdRefs;

typedef enum FuncType {
    TYPE_FUNC,
    TYPE_METHOD,
    TYPE_CTOR,
} FuncType;

typedef struct {
    JStarBuffer* items;
    size_t count, capacity;
} SyntheticNames;

struct Compiler {
    JStarVM* vm;
    ObjModule* module;

    const char* file;
    const JStarStmt* ast;

    int depth;
    Compiler* prev;

    Loop* loops;

    FuncType type;
    ObjFunction* func;

    uint8_t localsCount;
    Local locals[MAX_LOCALS];
    Upvalue upvalues[MAX_LOCALS];

    SyntheticNames syntheticNames;
    JStarIdentifiers* globals;
    FwdRefs* fwdRefs;

    int stackUsage;

    int tryDepth;
    TryBlock* tryBlocks;

    bool hadError;
};

static void initCompiler(Compiler* c, JStarVM* vm, Compiler* prev, const char* file, FuncType type,
                         const JStarStmt* ast, ObjModule* module, JStarIdentifiers* globals,
                         FwdRefs* fwdRefs) {
    vm->currCompiler = c;
    *c = (Compiler){
        .vm = vm,
        .module = module,
        .file = file,
        .ast = ast,
        .prev = prev,
        .type = type,
        .globals = globals,
        .fwdRefs = fwdRefs,
        // 1 for the receiver (always present)
        .stackUsage = 1,
    };
}

static void endCompiler(Compiler* c) {
    arrayForeach(JStarBuffer, b, &c->syntheticNames) {
        jsrBufferFree(b);
    }
    arrayFree(c->vm, &c->syntheticNames);
    if(c->prev != NULL) c->prev->hadError |= c->hadError;
    c->vm->currCompiler = c->prev;
}

// -----------------------------------------------------------------------------
// UTILITY FUNCTIONS
// -----------------------------------------------------------------------------

static void error(Compiler* c, JStarLoc loc, const char* format, ...) {
    JStarVM* vm = c->vm;
    c->hadError = true;

    if(vm->errorCallback) {
        JStarBuffer error;
        jsrBufferInitCapacity(c->vm, &error, strlen(format) * 2);

        va_list args;
        va_start(args, format);
        jsrBufferAppendvf(&error, format, args);
        va_end(args);

        vm->errorCallback(vm, JSR_COMPILE_ERR, c->file, loc, error.data);
        jsrBufferFree(&error);
    }
}

static void adjustStackUsage(Compiler* c, int num) {
    c->stackUsage += num;
    if(c->stackUsage > c->func->stackUsage) {
        c->func->stackUsage = c->stackUsage;
    }
}

static size_t emitOpcode(Compiler* c, Opcode op, int line) {
    adjustStackUsage(c, opcodeStackUsage(op));
    return writeByte(c->vm, &c->func->code, op, line);
}

static size_t emitByte(Compiler* c, uint8_t b, int line) {
    return writeByte(c->vm, &c->func->code, b, line);
}

static size_t emitShort(Compiler* c, uint16_t s, int line) {
    size_t addr = emitByte(c, (s >> 8) & 0xff, line);
    emitByte(c, s & 0xff, line);
    return addr;
}

static size_t getCurrentAddr(Compiler* c) {
    return c->func->code.bytecode.count;
}

static bool inGlobalScope(Compiler* c) {
    return c->depth == 0;
}

static void discardLocal(Compiler* c, const Local* local) {
    if(local->isUpvalue) {
        emitByte(c, OP_CLOSE_UPVALUE, local->loc.line);
    } else {
        emitByte(c, OP_POP, local->loc.line);
    }
}

static void enterScope(Compiler* c) {
    c->depth++;
}

static int discardScopes(Compiler* c, int depth, int line) {
    int locals = c->localsCount;
    while(locals > 0 && c->locals[locals - 1].depth > depth) {
        locals--;
    }

    int toPop = c->localsCount - locals;
    if(toPop > 1) {
        emitByte(c, OP_POPN, line);
        emitByte(c, toPop, line);
    } else if(toPop == 1) {
        discardLocal(c, &c->locals[locals]);
    }

    return toPop;
}

static void exitScope(Compiler* c, int line) {
    int popped = discardScopes(c, --c->depth, line);
    c->localsCount -= popped;
    c->stackUsage -= popped;
}

static void enterFunctionScope(Compiler* c) {
    c->depth++;
}

static void exitFunctionScope(Compiler* c) {
    c->depth--;
}

static uint16_t createConst(Compiler* c, Value constant, JStarLoc loc) {
    int index = addConstant(c->vm, &c->func->code, constant);
    if(index == -1) {
        error(c, loc, "Too many constants in function %s", c->func->proto.name->data);
        return 0;
    }
    return (uint16_t)index;
}

static JStarIdentifier createIdentifier(const char* name) {
    return (JStarIdentifier){strlen(name), name};
}

static JStarIdentifier createSyntheticIdentifier(Compiler* c, const char* fmt, ...) {
    JStarBuffer buf;
    va_list ap;
    va_start(ap, fmt);
    jsrBufferInitCapacity(c->vm, &buf, strlen(fmt) * 2);
    jsrBufferAppendvf(&buf, fmt, ap);
    arrayAppend(c->vm, &c->syntheticNames, buf);
    va_end(ap);
    return (JStarIdentifier){buf.size, buf.data};
}

static uint16_t stringConst(Compiler* c, const char* str, size_t length, JStarLoc loc) {
    ObjString* string = copyString(c->vm, str, length);
    return createConst(c, OBJ_VAL(string), loc);
}

static uint16_t identifierConst(Compiler* c, JStarIdentifier id, JStarLoc loc) {
    return stringConst(c, id.name, id.length, loc);
}

static uint16_t identifierSymbol(Compiler* c, JStarIdentifier id, JStarLoc loc) {
    int index = addSymbol(c->vm, &c->func->code, identifierConst(c, id, loc));
    if(index == -1) {
        error(c, loc, "Too many symbols in function %s", c->func->proto.name->data);
        return 0;
    }
    return (uint16_t)index;
}

static int addLocal(Compiler* c, JStarIdentifier id, JStarLoc loc) {
    if(c->localsCount == MAX_LOCALS) {
        error(c, loc, "Too many local variables in function %s", c->func->proto.name->data);
        return -1;
    }
    Local* local = &c->locals[c->localsCount];
    *local = (Local){.loc = loc, id, false, -1};
    return c->localsCount++;
}

static void initializeLocal(Compiler* c, int idx) {
    // Setting the depth field signals the local as initialized
    c->locals[idx].depth = c->depth;
}

static int resolveLocal(Compiler* c, JStarIdentifier id, JStarLoc loc) {
    for(int i = c->localsCount - 1; i >= 0; i--) {
        Local* local = &c->locals[i];
        if(jsrIdentifierEq(local->id, id)) {
            if(local->depth == -1) {
                error(c, loc, "Cannot read local variable `%.*s` in its own initializer",
                      local->id.length, local->id.name);
                return -1;
            }
            return i;
        }
    }
    return -1;
}

static int addUpvalue(Compiler* c, uint8_t index, bool local, JStarLoc loc) {
    uint8_t upvalueCount = c->func->upvalueCount;
    for(uint8_t i = 0; i < upvalueCount; i++) {
        Upvalue* upval = &c->upvalues[i];
        if(upval->index == index && upval->isLocal == local) {
            return i;
        }
    }

    if(c->func->upvalueCount == MAX_LOCALS) {
        error(c, loc, "Too many upvalues in function %s", c->func->proto.name->data);
        return -1;
    }

    c->upvalues[c->func->upvalueCount].isLocal = local;
    c->upvalues[c->func->upvalueCount].index = index;
    return c->func->upvalueCount++;
}

static int resolveUpvalue(Compiler* c, JStarIdentifier id, JStarLoc loc) {
    if(c->prev == NULL) {
        return -1;
    }

    int idx = resolveLocal(c->prev, id, loc);
    if(idx != -1) {
        int upvalueIdx = addUpvalue(c, idx, true, loc);
        c->prev->locals[idx].isUpvalue = true;
        return upvalueIdx;
    }

    idx = resolveUpvalue(c->prev, id, loc);
    if(idx != -1) {
        int upvalueIdx = addUpvalue(c, idx, false, loc);
        return upvalueIdx;
    }

    return idx;
}

static bool resolveGlobal(Compiler* c, JStarIdentifier id) {
    if(c->module) {
        if(hashTableIntGetString(&c->module->globalNames, id.name, id.length,
                                 hashBytes(id.name, id.length))) {
            return true;
        }
    } else if(resolveCoreSymbol(id)) {
        return true;
    }

    arrayForeach(JStarIdentifier, globalId, c->globals) {
        if(jsrIdentifierEq(id, *globalId)) {
            return true;
        }
    }

    return false;
}

static Variable resolveVar(Compiler* c, JStarIdentifier id, JStarLoc loc) {
    int localIdx = resolveLocal(c, id, loc);
    if(localIdx != -1) {
        return (Variable){VAR_LOCAL, {.local = {localIdx}}};
    }

    int upvalueIdx = resolveUpvalue(c, id, loc);
    if(upvalueIdx != -1) {
        return (Variable){VAR_UPVALUE, {.upvalue = {upvalueIdx}}};
    }

    if(resolveGlobal(c, id)) {
        return (Variable){VAR_GLOBAL, {.global = {id}}};
    }

    if(inGlobalScope(c)) {
        return (Variable){.scope = VAR_ERR};
    }

    FwdRef fwdRef = {id, loc};
    arrayAppend(c->vm, c->fwdRefs, fwdRef);
    return (Variable){VAR_GLOBAL, {.global = {id}}};
}

static void initializeVar(Compiler* c, const Variable* var) {
    JSR_ASSERT(var->scope == VAR_LOCAL, "Only local variables can be marked initialized");
    initializeLocal(c, var->as.local.localIdx);
}

static Variable declareVar(Compiler* c, JStarIdentifier id, bool forceLocal, JStarLoc loc) {
    if(inGlobalScope(c) && !forceLocal) {
        arrayAppend(c->vm, c->globals, id);
        return (Variable){VAR_GLOBAL, {.global = {id}}};
    }

    if(!inGlobalScope(c) && forceLocal) {
        error(c, loc, "static declaration can only appear in global scope");
        return (Variable){.scope = VAR_ERR};
    }

    for(int i = c->localsCount - 1; i >= 0; i--) {
        if(c->locals[i].depth != -1 && c->locals[i].depth < c->depth) break;
        if(jsrIdentifierEq(c->locals[i].id, id)) {
            Local* other = &c->locals[i];
            error(c, loc, "Variable `%.*s` already declared", id.length, id.name);
            error(c, other->loc, "NOTE: previous declaration of `%.*s` is here", id.length,
                  id.name);
            return (Variable){.scope = VAR_ERR};
        }
    }

    int index = addLocal(c, id, loc);
    if(index == -1) {
        return (Variable){.scope = VAR_ERR};
    }

    return (Variable){VAR_LOCAL, {.local = {index}}};
}

static void defineVar(Compiler* c, const Variable* var, JStarLoc loc) {
    switch(var->scope) {
    case VAR_GLOBAL:
        emitOpcode(c, OP_DEFINE_GLOBAL, loc.line);
        emitShort(c, identifierSymbol(c, var->as.global.id, loc), loc.line);
        break;
    case VAR_LOCAL:
        initializeVar(c, var);
        break;
    case VAR_ERR:
        // Nothing to do, error already reported
        break;
    case VAR_UPVALUE:
        JSR_UNREACHABLE();
    }
}

static void assertJumpOpcode(Opcode op) {
    JSR_ASSERT((op == OP_JUMP || op == OP_JUMPT || op == OP_JUMPF || op == OP_FOR_NEXT ||
                op == OP_SETUP_EXCEPT || op == OP_SETUP_ENSURE),
               "Not a jump opcode");
}

static size_t emitJumpTo(Compiler* c, Opcode jmpOp, size_t target, JStarLoc loc) {
    assertJumpOpcode(jmpOp);

    int32_t offset = target - (getCurrentAddr(c) + opcodeArgsNumber(jmpOp) + 1);
    if(offset > INT16_MAX || offset < INT16_MIN) {
        error(c, loc, "Too much code to jump over");
    }

    size_t jmpAddr = emitOpcode(c, jmpOp, loc.line);
    emitShort(c, (uint16_t)offset, loc.line);
    return jmpAddr;
}

static void setJumpTo(Compiler* c, size_t jumpAddr, size_t target, JStarLoc loc) {
    Code* code = &c->func->code;
    Opcode jmpOp = code->bytecode.items[jumpAddr];
    assertJumpOpcode(jmpOp);

    int32_t offset = target - (jumpAddr + opcodeArgsNumber(jmpOp) + 1);
    if(offset > INT16_MAX || offset < INT16_MIN) {
        error(c, loc, "Too much code to jump over");
    }

    code->bytecode.items[jumpAddr + 1] = ((uint16_t)offset >> 8) & 0xff;
    code->bytecode.items[jumpAddr + 2] = ((uint16_t)offset) & 0xff;
}

static void startLoop(Compiler* c, Loop* loop) {
    loop->depth = c->depth;
    loop->start = getCurrentAddr(c);
    loop->parent = c->loops;
    c->loops = loop;
}

static void patchLoopExitStmts(Compiler* c, size_t start, size_t contAddr, size_t brkAddr) {
    for(size_t i = start; i < getCurrentAddr(c); i++) {
        Opcode op = c->func->code.bytecode.items[i];
        if(op == OP_END) {
            c->func->code.bytecode.items[i] = OP_JUMP;

            // Patch jump with correct offset to break loop
            int mark = c->func->code.bytecode.items[i + 1];
            JSR_ASSERT(mark == CONTINUE_MARK || mark == BREAK_MARK, "Unknown loop breaking marker");

            setJumpTo(c, i, mark == CONTINUE_MARK ? contAddr : brkAddr, (JStarLoc){0});

            i += opcodeArgsNumber(OP_JUMP);
        } else {
            i += opcodeArgsNumber(op);
        }
    }
}

static void endLoop(Compiler* c) {
    JSR_ASSERT(c->loops, "Mismatched `startLoop` and `endLoop`");
    patchLoopExitStmts(c, c->loops->start, c->loops->start, getCurrentAddr(c));
    c->loops = c->loops->parent;
}

static void inlineMethodCall(Compiler* c, const char* name, int args, JStarLoc loc) {
    JSR_ASSERT(args <= MAX_INLINE_ARGS, "Too many arguments for inline call");
    JStarIdentifier methId = createIdentifier(name);
    emitOpcode(c, OP_INVOKE_0 + args, loc.line);
    emitShort(c, identifierSymbol(c, methId, loc), loc.line);
}

static void enterTryBlock(Compiler* c, TryBlock* exc, int numHandlers, JStarLoc loc) {
    exc->depth = c->depth;
    exc->numHandlers = numHandlers;
    exc->parent = c->tryBlocks;
    c->tryBlocks = exc;
    c->tryDepth += numHandlers;

    if(c->tryDepth > MAX_HANDLERS) {
        error(c, loc, "Exceeded max number of nested exception handlers: max %d, got %d",
              MAX_HANDLERS, c->tryDepth);
    }
}

static void exitTryBlock(Compiler* c) {
    JSR_ASSERT(c->tryBlocks, "Mismatched `enterTryBlock` and `exitTryBlock`");
    c->tryDepth -= c->tryBlocks->numHandlers;
    c->tryBlocks = c->tryBlocks->parent;
}

static ObjString* readString(Compiler* c, const JStarExpr* e) {
    const char* str = e->as.stringLiteral.str;
    size_t length = e->as.stringLiteral.length;

    JStarBuffer sb;
    jsrBufferInitCapacity(c->vm, &sb, length + 1);

    const int numEscapes = 11;
    const char* escaped = "\0\a\b\f\n\r\t\v\\\"'";
    const char* unescaped = "0abfnrtv\\\"'";

    for(size_t i = 0; i < length; i++) {
        if(str[i] == '\\') {
            int j = 0;
            for(j = 0; j < numEscapes; j++) {
                if(str[i + 1] == unescaped[j]) {
                    jsrBufferAppendChar(&sb, escaped[j]);
                    i++;
                    break;
                }
            }
            if(j == numEscapes) {
                error(c, e->loc, "Invalid escape character `%c`", str[i + 1]);
            }
        } else {
            jsrBufferAppendChar(&sb, str[i]);
        }
    }

    ObjString* string = copyString(c->vm, sb.data, sb.size);
    jsrBufferFree(&sb);

    return string;
}

static void addFunctionDefaults(Compiler* c, Prototype* proto, const JStarExprs* defaults) {
    int i = 0;
    arrayForeach(JStarExpr*, it, defaults) {
        const JStarExpr* e = *it;
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
            JSR_UNREACHABLE();
        }
    }
}

static JStarExpr* getExpressions(const JStarExpr* unpackable) {
    switch(unpackable->type) {
    case JSR_LIST:
        return unpackable->as.listLiteral.exprs;
    case JSR_TUPLE:
        return unpackable->as.tupleLiteral.exprs;
    default:
        JSR_UNREACHABLE();
    }
}

bool isSpreadExpr(const JStarExpr* e) {
    return e->type == JSR_SPREAD;
}

bool containsSpreadExpr(const JStarExpr* exprs) {
    JSR_ASSERT(exprs->type == JSR_EXPR_LST, "Not an expression list");
    arrayForeach(JStarExpr*, it, &exprs->as.exprList) {
        if(isSpreadExpr(*it)) {
            return true;
        }
    }
    return false;
}

// -----------------------------------------------------------------------------
// EXPRESSION COMPILE
// -----------------------------------------------------------------------------

static void compileExpr(Compiler* c, const JStarExpr* e);
static void compileListLit(Compiler* c, const JStarExpr* e);
static void compileFunction(Compiler* c, FuncType type, ObjString* name, const JStarStmt* node);

static void compileBinaryExpr(Compiler* c, const JStarExpr* e) {
    compileExpr(c, e->as.binary.left);
    compileExpr(c, e->as.binary.right);
    switch(e->as.binary.op) {
    case TOK_PLUS:
        emitOpcode(c, OP_ADD, e->loc.line);
        break;
    case TOK_MINUS:
        emitOpcode(c, OP_SUB, e->loc.line);
        break;
    case TOK_MULT:
        emitOpcode(c, OP_MUL, e->loc.line);
        break;
    case TOK_DIV:
        emitOpcode(c, OP_DIV, e->loc.line);
        break;
    case TOK_MOD:
        emitOpcode(c, OP_MOD, e->loc.line);
        break;
    case TOK_AMPER:
        emitOpcode(c, OP_BAND, e->loc.line);
        break;
    case TOK_PIPE:
        emitOpcode(c, OP_BOR, e->loc.line);
        break;
    case TOK_TILDE:
        emitOpcode(c, OP_XOR, e->loc.line);
        break;
    case TOK_LSHIFT:
        emitOpcode(c, OP_LSHIFT, e->loc.line);
        break;
    case TOK_RSHIFT:
        emitOpcode(c, OP_RSHIFT, e->loc.line);
        break;
    case TOK_EQUAL_EQUAL:
        emitOpcode(c, OP_EQ, e->loc.line);
        break;
    case TOK_GT:
        emitOpcode(c, OP_GT, e->loc.line);
        break;
    case TOK_GE:
        emitOpcode(c, OP_GE, e->loc.line);
        break;
    case TOK_LT:
        emitOpcode(c, OP_LT, e->loc.line);
        break;
    case TOK_LE:
        emitOpcode(c, OP_LE, e->loc.line);
        break;
    case TOK_IS:
        emitOpcode(c, OP_IS, e->loc.line);
        break;
    case TOK_BANG_EQ:
        emitOpcode(c, OP_EQ, e->loc.line);
        emitOpcode(c, OP_NOT, e->loc.line);
        break;
    default:
        JSR_UNREACHABLE();
    }
}

static void compileLogicExpr(Compiler* c, const JStarExpr* e) {
    compileExpr(c, e->as.binary.left);
    emitOpcode(c, OP_DUP, e->loc.line);

    Opcode jmpOp = e->as.binary.op == TOK_AND ? OP_JUMPF : OP_JUMPT;
    size_t shortCircuit = emitOpcode(c, jmpOp, e->loc.line);
    emitShort(c, 0, e->loc.line);

    emitOpcode(c, OP_POP, e->loc.line);
    compileExpr(c, e->as.binary.right);

    setJumpTo(c, shortCircuit, getCurrentAddr(c), e->loc);
}

static void compileUnaryExpr(Compiler* c, const JStarExpr* e) {
    compileExpr(c, e->as.unary.operand);
    switch(e->as.unary.op) {
    case TOK_MINUS:
        emitOpcode(c, OP_NEG, e->loc.line);
        break;
    case TOK_BANG:
        emitOpcode(c, OP_NOT, e->loc.line);
        break;
    case TOK_TILDE:
        emitOpcode(c, OP_INVERT, e->loc.line);
        break;
    case TOK_HASH:
        inlineMethodCall(c, "__len__", 0, e->loc);
        break;
    case TOK_HASH_HASH:
        inlineMethodCall(c, "__string__", 0, e->loc);
        break;
    default:
        JSR_UNREACHABLE();
    }
}

static void compileTernaryExpr(Compiler* c, const JStarExpr* e) {
    compileExpr(c, e->as.ternary.cond);

    size_t falseJmp = emitOpcode(c, OP_JUMPF, e->loc.line);
    emitShort(c, 0, e->loc.line);

    compileExpr(c, e->as.ternary.thenExpr);
    size_t exitJmp = emitOpcode(c, OP_JUMP, e->loc.line);
    emitShort(c, 0, e->loc.line);

    setJumpTo(c, falseJmp, getCurrentAddr(c), e->loc);
    compileExpr(c, e->as.ternary.elseExpr);

    setJumpTo(c, exitJmp, getCurrentAddr(c), e->loc);
}

static void compileVarLit(Compiler* c, JStarIdentifier id, bool set, JStarLoc loc) {
    Variable var = resolveVar(c, id, loc);
    switch(var.scope) {
    case VAR_LOCAL:
        if(set) {
            emitOpcode(c, OP_SET_LOCAL, loc.line);
        } else {
            emitOpcode(c, OP_GET_LOCAL, loc.line);
        }
        emitByte(c, var.as.local.localIdx, loc.line);
        break;
    case VAR_UPVALUE:
        if(set) {
            emitOpcode(c, OP_SET_UPVALUE, loc.line);
        } else {
            emitOpcode(c, OP_GET_UPVALUE, loc.line);
        }
        emitByte(c, var.as.upvalue.upvalueIdx, loc.line);
        break;
    case VAR_GLOBAL:
        if(set) {
            emitOpcode(c, OP_SET_GLOBAL, loc.line);
        } else {
            emitOpcode(c, OP_GET_GLOBAL, loc.line);
        }
        emitShort(c, identifierSymbol(c, var.as.global.id, loc), loc.line);
        break;
    case VAR_ERR:
        error(c, loc, "Cannot resolve name `%.*s`", id.length, id.name);
        break;
    }
}

static void compileFunLiteral(Compiler* c, const JStarExpr* e, JStarIdentifier name) {
    JStarStmt* func = e->as.funLit.func;
    if(!name.name) {
        JStarBuffer buf;
        jsrBufferInitCapacity(c->vm, &buf, sizeof(ANON_FMT) * 2);
        jsrBufferAppendf(&buf, ANON_FMT, func->loc.line, func->loc.col);
        compileFunction(c, TYPE_FUNC, jsrBufferToString(&buf), func);
    } else {
        func->as.decl.as.fun.id = name;
        compileFunction(c, TYPE_FUNC, copyString(c->vm, name.name, name.length), func);
    }
}

static void compileLval(Compiler* c, const JStarExpr* e) {
    switch(e->type) {
    case JSR_VAR:
        compileVarLit(c, e->as.varLiteral.id, true, e->loc);
        break;
    case JSR_PROPERTY_ACCESS: {
        compileExpr(c, e->as.propertyAccess.left);
        emitOpcode(c, OP_SET_FIELD, e->loc.line);
        emitShort(c, identifierSymbol(c, e->as.propertyAccess.id, e->loc), e->loc.line);
        break;
    }
    case JSR_INDEX: {
        compileExpr(c, e->as.index.index);
        compileExpr(c, e->as.index.left);
        emitOpcode(c, OP_SUBSCR_SET, e->loc.line);
        break;
    }
    default:
        JSR_UNREACHABLE();
    }
}

// The `name` argument is the name of the variable to which we are assigning to.
// In case of a function literal we use it to give the function a meaningful name, instead
// of just using the default name for anonymous functions (that is: `anon:<line_number>`)
static void compileRval(Compiler* c, const JStarExpr* e, JStarIdentifier name) {
    if(e->type == JSR_FUN_LIT) {
        compileFunLiteral(c, e, name);
    } else {
        compileExpr(c, e);
    }
}

static void compileConstUnpack(Compiler* c, const JStarExpr* exprs, int lvals,
                               const JStarIdentifiers* names) {
    if(exprs->as.exprList.count < (size_t)lvals) {
        error(c, exprs->loc, "Too few values to unpack: expected %d, got %zu", lvals,
              exprs->as.exprList.count);
    }

    int i = 0;
    arrayForeach(JStarExpr*, it, &exprs->as.exprList) {
        JStarIdentifier name = {0};
        if(names->count && i < lvals) {
            name = names->items[i];
        }

        JStarExpr* e = *it;
        compileRval(c, e, name);

        if(++i > lvals) {
            emitOpcode(c, OP_POP, e->loc.line);
        }
    }
}

// Compile an unpack assignment of the form: a, b, ..., z = ...
static void compileUnpackAssign(Compiler* c, const JStarExpr* e) {
    JStarExpr* lvals = e->as.assign.lval->as.tupleLiteral.exprs;
    size_t lvalCount = lvals->as.exprList.count;

    if(lvalCount >= UINT8_MAX) {
        error(c, e->loc, "Exceeded max number of unpack assignment: %d", UINT8_MAX);
    }

    JStarExpr* rval = e->as.assign.rval;

    // Optimize constant unpacks by omitting tuple allocation
    if(IS_CONST_UNPACK(rval->type)) {
        JStarExpr* exprs = getExpressions(rval);
        compileConstUnpack(c, exprs, lvalCount, &(JStarIdentifiers){0});
    } else {
        compileRval(c, rval, (JStarIdentifier){0});
        emitOpcode(c, OP_UNPACK, e->loc.line);
        emitByte(c, (uint8_t)lvalCount, e->loc.line);
        adjustStackUsage(c, lvalCount - 1);
    }

    // compile lvals in reverse order in order to assign
    // correct values to variables in case of a const unpack
    for(int n = lvalCount - 1; n >= 0; n--) {
        JStarExpr* lval = lvals->as.exprList.items[n];
        compileLval(c, lval);
        if(n != 0) emitOpcode(c, OP_POP, e->loc.line);
    }
}

static void compileAssignExpr(Compiler* c, const JStarExpr* e) {
    switch(e->as.assign.lval->type) {
    case JSR_VAR: {
        JStarIdentifier name = e->as.assign.lval->as.varLiteral.id;
        compileRval(c, e->as.assign.rval, name);
        compileLval(c, e->as.assign.lval);
        break;
    }
    case JSR_PROPERTY_ACCESS: {
        JStarIdentifier name = e->as.assign.lval->as.propertyAccess.id;
        compileRval(c, e->as.assign.rval, name);
        compileLval(c, e->as.assign.lval);
        break;
    }
    case JSR_INDEX: {
        compileRval(c, e->as.assign.rval, (JStarIdentifier){0});
        compileLval(c, e->as.assign.lval);
        break;
    }
    case JSR_TUPLE: {
        compileUnpackAssign(c, e);
        break;
    }
    default:
        JSR_UNREACHABLE();
    }
}

static void compileCompundAssign(Compiler* c, const JStarExpr* e) {
    JStarTokType op = e->as.compoundAssign.op;
    JStarExpr* l = e->as.compoundAssign.lval;
    JStarExpr* r = e->as.compoundAssign.rval;

    // expand compound assignement (e.g. a op= b -> a = a op b)
    JStarExpr binary = {e->loc, JSR_BINARY, {.binary = {op, l, r}}};
    JStarExpr assignment = {e->loc, JSR_ASSIGN, {.assign = {l, &binary}}};

    // compile as a normal assignment
    compileAssignExpr(c, &assignment);
}

static void compileArguments(Compiler* c, const JStarExpr* args) {
    arrayForeach(JStarExpr*, it, &args->as.exprList) {
        compileExpr(c, *it);
    }

    if(args->as.exprList.count >= UINT8_MAX) {
        error(c, args->loc, "Exceeded maximum number of arguments (%d) for function %s",
              (int)UINT8_MAX, c->func->proto.name->data);
    }
}

static void compileUnpackArguments(Compiler* c, JStarExpr* args) {
    JStarExpr argsList = (JStarExpr){args->loc, JSR_LIST, .as = {.listLiteral = {args}}};
    compileListLit(c, &argsList);
}

static void emitCallOp(Compiler* c, Opcode callCode, Opcode callInline, Opcode callUnpack,
                       uint8_t argsCount, bool unpackCall, int line) {
    if(unpackCall) {
        emitOpcode(c, callUnpack, line);
    } else if(argsCount <= MAX_INLINE_ARGS) {
        emitOpcode(c, callInline + argsCount, line);
    } else {
        emitOpcode(c, callCode, line);
        emitByte(c, argsCount, line);
    }
}

static void compileCallExpr(Compiler* c, const JStarExpr* e) {
    Opcode callCode = OP_CALL;
    Opcode callInline = OP_CALL_0;
    Opcode callUnpack = OP_CALL_UNPACK;

    JStarExpr* callee = e->as.call.callee;
    bool isMethod = callee->type == JSR_PROPERTY_ACCESS;

    if(isMethod) {
        callCode = OP_INVOKE;
        callInline = OP_INVOKE_0;
        callUnpack = OP_INVOKE_UNPACK;
        compileExpr(c, callee->as.propertyAccess.left);
    } else {
        compileExpr(c, callee);
    }

    JStarExpr* args = e->as.call.args;
    bool unpackCall = containsSpreadExpr(args);

    if(unpackCall) {
        compileUnpackArguments(c, args);
    } else {
        compileArguments(c, args);
    }

    uint8_t argsCount = args->as.exprList.count;
    emitCallOp(c, callCode, callInline, callUnpack, argsCount, unpackCall, e->loc.line);

    if(isMethod) {
        emitShort(c, identifierSymbol(c, callee->as.propertyAccess.id, e->loc), e->loc.line);
    }
}

static void compileSuper(Compiler* c, const JStarExpr* e) {
    // TODO: replace check to support super calls in closures nested in methods
    if(c->type != TYPE_METHOD && c->type != TYPE_CTOR) {
        error(c, e->loc, "Can only use `super` in method call");
        return;
    }

    // place `this` on top of the stack
    emitOpcode(c, OP_GET_LOCAL, e->loc.line);
    emitByte(c, 0, e->loc.line);

    uint16_t methodSym;
    if(e->as.sup.name.name != NULL) {
        methodSym = identifierSymbol(c, e->as.sup.name, e->loc);
    } else {
        methodSym = identifierSymbol(c, c->ast->as.decl.as.fun.id, e->loc);
    }

    JStarIdentifier superId = createIdentifier("super");
    JStarExpr* args = e->as.sup.args;

    if(args != NULL) {
        bool unpackCall = containsSpreadExpr(args);

        if(unpackCall) {
            compileUnpackArguments(c, args);
        } else {
            compileArguments(c, args);
        }

        uint8_t argsCount = args->as.exprList.count;
        compileVarLit(c, superId, false, e->loc);
        emitCallOp(c, OP_SUPER, OP_SUPER_0, OP_SUPER_UNPACK, argsCount, unpackCall, e->loc.line);
        emitShort(c, methodSym, e->loc.line);
    } else {
        compileVarLit(c, superId, false, e->loc);
        emitOpcode(c, OP_SUPER_BIND, e->loc.line);
        emitShort(c, methodSym, e->loc.line);
    }
}

static void compileAccessExpression(Compiler* c, const JStarExpr* e) {
    compileExpr(c, e->as.propertyAccess.left);
    emitOpcode(c, OP_GET_FIELD, e->loc.line);
    emitShort(c, identifierSymbol(c, e->as.propertyAccess.id, e->loc), e->loc.line);
}

static void compileArraryAccExpression(Compiler* c, const JStarExpr* e) {
    compileExpr(c, e->as.index.left);
    compileExpr(c, e->as.index.index);
    emitOpcode(c, OP_SUBSCR_GET, e->loc.line);
}

static void compilePowExpr(Compiler* c, const JStarExpr* e) {
    compileExpr(c, e->as.pow.base);
    compileExpr(c, e->as.pow.exp);
    emitOpcode(c, OP_POW, e->loc.line);
}

static void compileListLit(Compiler* c, const JStarExpr* e) {
    emitOpcode(c, OP_NEW_LIST, e->loc.line);

    arrayForeach(JStarExpr*, it, &e->as.listLiteral.exprs->as.exprList) {
        JStarExpr* expr = *it;

        if(isSpreadExpr(expr)) {
            emitOpcode(c, OP_DUP, e->loc.line);
        }

        compileExpr(c, expr);

        if(isSpreadExpr(expr)) {
            inlineMethodCall(c, "addAll", 1, expr->loc);
            emitOpcode(c, OP_POP, expr->loc.line);
        } else {
            emitOpcode(c, OP_APPEND_LIST, e->loc.line);
        }
    }
}

static void compileSpreadTupleLit(Compiler* c, const JStarExpr* e) {
    JStarExpr toList = (JStarExpr){e->loc, JSR_LIST,
                                   .as = {.listLiteral = {e->as.tupleLiteral.exprs}}};
    compileListLit(c, &toList);
    emitOpcode(c, OP_LIST_TO_TUPLE, e->loc.line);
}

static void compileTupleLit(Compiler* c, const JStarExpr* e) {
    if(containsSpreadExpr(e->as.tupleLiteral.exprs)) {
        compileSpreadTupleLit(c, e);
        return;
    }

    arrayForeach(JStarExpr*, it, &e->as.tupleLiteral.exprs->as.exprList) {
        compileExpr(c, *it);
    }

    size_t tupleCount = e->as.tupleLiteral.exprs->as.exprList.count;
    if(tupleCount >= UINT8_MAX) {
        error(c, e->loc, "Too many elements in Tuple literal");
    }

    emitOpcode(c, OP_NEW_TUPLE, e->loc.line);
    emitByte(c, tupleCount, e->loc.line);
}

static void compileTableLit(Compiler* c, const JStarExpr* e) {
    emitOpcode(c, OP_NEW_TABLE, e->loc.line);

    JStarExpr* keyVals = e->as.tableLiteral.keyVals;
    for(size_t i = 0; i < keyVals->as.exprList.count;) {
        JStarExpr* expr = keyVals->as.exprList.items[i];

        if(isSpreadExpr(expr)) {
            emitOpcode(c, OP_DUP, e->loc.line);
            compileExpr(c, expr);
            inlineMethodCall(c, "addAll", 1, e->loc);
            emitOpcode(c, OP_POP, e->loc.line);

            i += 1;
        } else {
            JStarExpr* val = keyVals->as.exprList.items[i + 1];

            emitOpcode(c, OP_DUP, e->loc.line);
            compileExpr(c, expr);
            compileExpr(c, val);

            inlineMethodCall(c, "__set__", 2, e->loc);
            emitOpcode(c, OP_POP, e->loc.line);

            i += 2;
        }
    }
}

static void compileYield(Compiler* c, const JStarExpr* e) {
    if(c->type == TYPE_CTOR) {
        error(c, e->loc, "Cannot use yield in constructor");
    }

    if(e->as.yield.expr != NULL) {
        compileExpr(c, e->as.yield.expr);
    } else {
        emitOpcode(c, OP_NULL, e->loc.line);
    }

    emitOpcode(c, OP_YIELD, e->loc.line);
}

static void emitValueConst(Compiler* c, Value val, JStarLoc loc) {
    emitOpcode(c, OP_GET_CONST, loc.line);
    emitShort(c, createConst(c, val, loc), loc.line);
}

static void compileExpr(Compiler* c, const JStarExpr* e) {
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
    case JSR_COMPOUND_ASSIGN:
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
    case JSR_PROPERTY_ACCESS:
        compileAccessExpression(c, e);
        break;
    case JSR_INDEX:
        compileArraryAccExpression(c, e);
        break;
    case JSR_YIELD:
        compileYield(c, e);
        break;
    case JSR_POWER:
        compilePowExpr(c, e);
        break;
    case JSR_NUMBER:
        emitValueConst(c, NUM_VAL(e->as.num), e->loc);
        break;
    case JSR_BOOL:
        emitValueConst(c, BOOL_VAL(e->as.boolean), e->loc);
        break;
    case JSR_STRING:
        emitValueConst(c, OBJ_VAL(readString(c, e)), e->loc);
        break;
    case JSR_VAR:
        compileVarLit(c, e->as.varLiteral.id, false, e->loc);
        break;
    case JSR_NULL:
        emitOpcode(c, OP_NULL, e->loc.line);
        break;
    case JSR_LIST:
        compileListLit(c, e);
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
    case JSR_FUN_LIT:
        compileFunLiteral(c, e, (JStarIdentifier){0});
        break;
    case JSR_SPREAD:
        compileExpr(c, e->as.spread.expr);
        break;
    case JSR_EXPR_LST:
        arrayForeach(JStarExpr*, it, &e->as.exprList) {
            compileExpr(c, *it);
        }
        break;
    }
}

// -----------------------------------------------------------------------------
// STATEMENT COMPILE
// -----------------------------------------------------------------------------

// Control flow statements

static void compileStatement(Compiler* c, const JStarStmt* s);

static void compileStatements(Compiler* c, const JStarStmts* stmts) {
    arrayForeach(JStarStmt*, it, stmts) {
        compileStatement(c, *it);
    }
}

static void compileReturnStatement(Compiler* c, const JStarStmt* s) {
    if(c->type == TYPE_CTOR) {
        error(c, s->loc, "Cannot use return in constructor");
    }

    if(s->as.returnStmt.e != NULL) {
        compileExpr(c, s->as.returnStmt.e);
    } else {
        emitOpcode(c, OP_NULL, s->loc.line);
    }

    if(c->ast->as.decl.as.fun.isGenerator) {
        emitOpcode(c, OP_GENERATOR_CLOSE, s->loc.line);
    }

    emitOpcode(c, OP_RETURN, s->loc.line);
}

static void compileIfStatement(Compiler* c, const JStarStmt* s) {
    compileExpr(c, s->as.ifStmt.cond);

    size_t falseJmp = emitOpcode(c, OP_JUMPF, s->loc.line);
    emitShort(c, 0, s->loc.line);

    compileStatement(c, s->as.ifStmt.thenStmt);

    size_t exitJmp = 0;
    if(s->as.ifStmt.elseStmt != NULL) {
        exitJmp = emitOpcode(c, OP_JUMP, s->loc.line);
        emitShort(c, 0, s->loc.line);
    }

    setJumpTo(c, falseJmp, getCurrentAddr(c), s->loc);

    if(s->as.ifStmt.elseStmt != NULL) {
        compileStatement(c, s->as.ifStmt.elseStmt);
        setJumpTo(c, exitJmp, getCurrentAddr(c), s->loc);
    }
}

static void compileForStatement(Compiler* c, const JStarStmt* s) {
    enterScope(c);

    if(s->as.forStmt.init != NULL) {
        compileStatement(c, s->as.forStmt.init);
    }

    size_t firstJmp = 0;
    if(s->as.forStmt.act != NULL) {
        firstJmp = emitOpcode(c, OP_JUMP, s->loc.line);
        emitShort(c, 0, s->loc.line);
    }

    Loop l;
    startLoop(c, &l);

    if(s->as.forStmt.act != NULL) {
        compileExpr(c, s->as.forStmt.act);
        emitOpcode(c, OP_POP, s->loc.line);
        setJumpTo(c, firstJmp, getCurrentAddr(c), s->loc);
    }

    size_t exitJmp = 0;
    if(s->as.forStmt.cond != NULL) {
        compileExpr(c, s->as.forStmt.cond);
        exitJmp = emitOpcode(c, OP_JUMPF, s->loc.line);
        emitShort(c, 0, s->loc.line);
    }

    compileStatement(c, s->as.forStmt.body);
    emitJumpTo(c, OP_JUMP, l.start, s->loc);

    if(s->as.forStmt.cond != NULL) {
        setJumpTo(c, exitJmp, getCurrentAddr(c), s->loc);
    }

    endLoop(c);
    exitScope(c, s->loc.line);
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
static void compileForEach(Compiler* c, const JStarStmt* s) {
    enterScope(c);

    // Evaluate `iterable` once and store it in a local
    JStarIdentifier exprName = createIdentifier(".expr");
    Variable exprVar = declareVar(c, exprName, false, s->as.forEach.iterable->loc);
    defineVar(c, &exprVar, s->as.forEach.iterable->loc);
    compileExpr(c, s->as.forEach.iterable);

    // Set the iterator variable with a name that it's not an identifier.
    JStarIdentifier iteratorName = createIdentifier(".iter");
    Variable iterVar = declareVar(c, iteratorName, false, s->loc);
    defineVar(c, &iterVar, s->loc);
    emitOpcode(c, OP_NULL, s->loc.line);  // initialize `.iter` to null

    // FOR_PREP will cache the __iter__ and __next__ methods as locals
    emitOpcode(c, OP_FOR_PREP, s->loc.line);

    // Declare variables for cached methods
    JStarIdentifier iterMethName = createIdentifier(".__iter__");
    Variable iterMethVar = declareVar(c, iterMethName, false, s->loc);
    defineVar(c, &iterMethVar, s->loc);

    JStarIdentifier nextMethName = createIdentifier(".__next__");
    Variable nextMethVar = declareVar(c, nextMethName, false, s->loc);
    defineVar(c, &nextMethVar, s->loc);

    Loop l;
    startLoop(c, &l);

    emitOpcode(c, OP_FOR_ITER, s->loc.line);
    size_t exitJmp = emitOpcode(c, OP_FOR_NEXT, s->loc.line);
    emitShort(c, 0, s->loc.line);

    JStarStmt* varDecl = s->as.forEach.var;
    enterScope(c);

    arrayForeach(JStarIdentifier, name, &varDecl->as.decl.as.var.ids) {
        Variable var = declareVar(c, *name, false, s->loc);
        defineVar(c, &var, s->loc);
    }

    uint8_t declCount = varDecl->as.decl.as.var.ids.count;
    if(varDecl->as.decl.as.var.isUnpack) {
        emitOpcode(c, OP_UNPACK, s->loc.line);
        emitByte(c, declCount, s->loc.line);
        adjustStackUsage(c, declCount);
    }

    JStarStmt* body = s->as.forEach.body;
    compileStatements(c, &body->as.blockStmt.stmts);

    exitScope(c, s->loc.line);

    emitJumpTo(c, OP_JUMP, l.start, s->loc);
    setJumpTo(c, exitJmp, getCurrentAddr(c), s->loc);

    endLoop(c);
    exitScope(c, s->loc.line);
}

static void compileWhileStatement(Compiler* c, const JStarStmt* s) {
    Loop l;
    startLoop(c, &l);

    compileExpr(c, s->as.whileStmt.cond);
    size_t exitJmp = emitOpcode(c, OP_JUMPF, s->loc.line);
    emitShort(c, 0, s->loc.line);

    compileStatement(c, s->as.whileStmt.body);

    emitJumpTo(c, OP_JUMP, l.start, s->loc);
    setJumpTo(c, exitJmp, getCurrentAddr(c), s->loc);

    endLoop(c);
}

static void compileImportStatement(Compiler* c, const JStarStmt* s) {
    const JStarIdentifiers* modules = &s->as.importStmt.modules;
    const JStarIdentifiers* names = &s->as.importStmt.names;
    bool importFor = names->count > 0;
    bool importAs = s->as.importStmt.as.name != NULL;

    JStarBuffer nameBuf;
    jsrBufferInit(c->vm, &nameBuf);

    Variable modVar;
    if(!importFor) {
        modVar = declareVar(c, importAs ? s->as.importStmt.as : modules->items[0], false, s->loc);
    }

    arrayForeach(JStarIdentifier, submoduleName, modules) {
        jsrBufferAppend(&nameBuf, submoduleName->name, submoduleName->length);

        if(importAs && submoduleName == modules->items + modules->count - 1) {
            emitOpcode(c, OP_IMPORT, s->loc.line);
        } else if(submoduleName == modules->items && !(importAs || importFor)) {
            emitOpcode(c, OP_IMPORT, s->loc.line);
        } else {
            emitOpcode(c, OP_IMPORT_FROM, s->loc.line);
        }

        emitShort(c, stringConst(c, nameBuf.data, nameBuf.size, s->loc), s->loc.line);
        emitOpcode(c, OP_POP, s->loc.line);

        if(submoduleName != modules->items + modules->count - 1) {
            jsrBufferAppendChar(&nameBuf, '.');
        }
    }

    if(importFor) {
        arrayForeach(JStarIdentifier, name, names) {
            emitOpcode(c, OP_IMPORT_NAME, s->loc.line);
            emitShort(c, stringConst(c, nameBuf.data, nameBuf.size, s->loc), s->loc.line);
            emitShort(c, identifierConst(c, *name, s->loc), s->loc.line);

            Variable nameVar = declareVar(c, *name, false, s->loc);
            defineVar(c, &nameVar, s->loc);
        }
    } else {
        defineVar(c, &modVar, s->loc);
    }

    jsrBufferFree(&nameBuf);
}

static void compileExcepts(Compiler* c, const JStarStmts* excepts, size_t curr) {
    const JStarStmt* except = excepts->items[curr];

    // Retrieve the exception by using its phny variable name
    JStarIdentifier exceptionId = createIdentifier(".exception");
    compileVarLit(c, exceptionId, false, except->loc);

    // Test the exception's class
    compileExpr(c, except->as.excStmt.cls);
    emitOpcode(c, OP_IS, except->loc.line);

    size_t falseJmp = emitOpcode(c, OP_JUMPF, except->loc.line);
    emitShort(c, 0, except->loc.line);

    // Compile the handler code
    enterScope(c);
    adjustStackUsage(c, 1);

    // Retrieve the exception again, this time binding it to a local
    compileVarLit(c, exceptionId, false, except->loc);
    Variable excVar = declareVar(c, except->as.excStmt.var, false, except->loc);
    defineVar(c, &excVar, except->loc);

    JStarStmt* body = except->as.excStmt.block;
    compileStatements(c, &body->as.blockStmt.stmts);

    // Set the exception cause to `null` to signal that the exception has been handled
    JStarIdentifier causeName = createIdentifier(".cause");
    emitOpcode(c, OP_NULL, except->loc.line);
    compileVarLit(c, causeName, true, except->loc);
    emitOpcode(c, OP_POP, except->loc.line);

    adjustStackUsage(c, -1);
    exitScope(c, except->loc.line);

    size_t exitJmp = 0;
    if(curr < excepts->count - 1) {
        exitJmp = emitOpcode(c, OP_JUMP, except->loc.line);
        emitShort(c, 0, except->loc.line);
    }

    setJumpTo(c, falseJmp, getCurrentAddr(c), except->loc);

    // Compile the next handler
    if(curr < excepts->count - 1) {
        compileExcepts(c, excepts, curr + 1);
        setJumpTo(c, exitJmp, getCurrentAddr(c), except->loc);
    }
}

static void compileTryExcept(Compiler* c, const JStarStmt* s) {
    bool hasExcepts = s->as.tryStmt.excs.count > 0;
    bool hasEnsure = s->as.tryStmt.ensure != NULL;
    int numHandlers = (hasExcepts ? 1 : 0) + (hasEnsure ? 1 : 0);

    TryBlock tryBlock;
    enterTryBlock(c, &tryBlock, numHandlers, s->loc);

    size_t ensureSetup = 0, exceptSetup = 0;

    if(hasEnsure) {
        ensureSetup = emitOpcode(c, OP_SETUP_ENSURE, s->loc.line);
        emitShort(c, 0, s->loc.line);
    }

    if(hasExcepts) {
        exceptSetup = emitOpcode(c, OP_SETUP_EXCEPT, s->loc.line);
        emitShort(c, 0, s->loc.line);
    }

    compileStatement(c, s->as.tryStmt.block);

    if(hasExcepts) {
        emitOpcode(c, OP_POP_HANDLER, s->loc.line);
    }

    if(hasEnsure) {
        emitOpcode(c, OP_POP_HANDLER, s->loc.line);
        // Reached end of try block during normal execution flow, set exception and unwind
        // cause to null to signal the ensure handler that no exception was raised
        emitOpcode(c, OP_NULL, s->loc.line);
        emitOpcode(c, OP_NULL, s->loc.line);
    }

    enterScope(c);

    // Phony variable for the exception
    JStarIdentifier exceptionName = createIdentifier(".exception");
    Variable excVar = declareVar(c, exceptionName, false, s->loc);
    defineVar(c, &excVar, s->loc);

    // Phony variable for the unwing cause enum
    JStarIdentifier causeName = createIdentifier(".cause");
    Variable causeVar = declareVar(c, causeName, false, s->loc);
    defineVar(c, &causeVar, s->loc);

    // Compile excepts (if any)
    if(hasExcepts) {
        size_t excJmp = emitOpcode(c, OP_JUMP, s->loc.line);
        emitShort(c, 0, s->loc.line);

        setJumpTo(c, exceptSetup, getCurrentAddr(c), s->loc);
        compileExcepts(c, &s->as.tryStmt.excs, 0);

        if(hasEnsure) {
            emitOpcode(c, OP_POP_HANDLER, s->loc.line);
        } else {
            emitOpcode(c, OP_END_HANDLER, s->loc.line);
            exitScope(c, s->loc.line);
        }

        setJumpTo(c, excJmp, getCurrentAddr(c), s->loc);
    }

    // Compile ensure block (if any)
    if(hasEnsure) {
        setJumpTo(c, ensureSetup, getCurrentAddr(c), s->loc);
        compileStatement(c, s->as.tryStmt.ensure);
        emitOpcode(c, OP_END_HANDLER, s->loc.line);
        exitScope(c, s->loc.line);
    }

    exitTryBlock(c);
}

static void compileRaiseStmt(Compiler* c, const JStarStmt* s) {
    compileExpr(c, s->as.raiseStmt.exc);
    emitOpcode(c, OP_RAISE, s->loc.line);
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
static void compileWithStatement(Compiler* c, const JStarStmt* s) {
    enterScope(c);

    // var x
    emitOpcode(c, OP_NULL, s->loc.line);
    Variable var = declareVar(c, s->as.withStmt.var, false, s->loc);
    defineVar(c, &var, s->loc);

    // try
    TryBlock tryBlock;
    enterTryBlock(c, &tryBlock, 1, s->loc);

    size_t ensSetup = emitOpcode(c, OP_SETUP_ENSURE, s->loc.line);
    emitShort(c, 0, s->loc.line);

    // x = closable
    JStarExpr lval = {
        .loc = s->loc,
        .type = JSR_VAR,
        .as = {.varLiteral = {s->as.withStmt.var}},
    };
    JStarExpr assign = {
        .loc = s->loc,
        .type = JSR_ASSIGN,
        .as = {.assign = {.lval = &lval, .rval = s->as.withStmt.e}},
    };
    compileExpr(c, &assign);
    emitOpcode(c, OP_POP, s->loc.line);

    // code
    compileStatement(c, s->as.withStmt.block);

    emitOpcode(c, OP_POP_HANDLER, s->loc.line);
    emitOpcode(c, OP_NULL, s->loc.line);
    emitOpcode(c, OP_NULL, s->loc.line);

    // ensure
    enterScope(c);

    JStarIdentifier exceptionName = createIdentifier(".exception");
    Variable excVar = declareVar(c, exceptionName, false, s->loc);
    defineVar(c, &excVar, s->loc);

    JStarIdentifier causeName = createIdentifier(".cause");
    Variable causeVar = declareVar(c, causeName, false, s->loc);
    defineVar(c, &causeVar, s->loc);

    setJumpTo(c, ensSetup, getCurrentAddr(c), s->loc);

    // if x then x.close() end
    compileVarLit(c, s->as.withStmt.var, false, s->loc);
    size_t falseJmp = emitOpcode(c, OP_JUMPF, s->loc.line);
    emitShort(c, 0, s->loc.line);

    compileVarLit(c, s->as.withStmt.var, false, s->loc);
    inlineMethodCall(c, "close", 0, s->loc);
    emitOpcode(c, OP_POP, s->loc.line);

    setJumpTo(c, falseJmp, getCurrentAddr(c), s->loc);

    emitOpcode(c, OP_END_HANDLER, s->loc.line);
    exitScope(c, s->loc.line);

    exitTryBlock(c);
    exitScope(c, s->loc.line);
}

static void compileLoopExitStmt(Compiler* c, const JStarStmt* s) {
    bool isBreak = s->type == JSR_BREAK;

    if(c->loops == NULL) {
        error(c, s->loc, "Cannot use %s outside loop", isBreak ? "break" : "continue");
        return;
    }

    if(c->tryDepth != 0 && c->tryBlocks->depth >= c->loops->depth) {
        error(c, s->loc, "Cannot %s out of a try block", isBreak ? "break" : "continue");
    }

    discardScopes(c, c->loops->depth, s->loc.line);

    // Emit place-holder instruction that will be patched at the end of loop compilation
    // when we know the offset to emit for a break or continue jump
    emitOpcode(c, OP_END, s->loc.line);
    emitByte(c, isBreak ? BREAK_MARK : CONTINUE_MARK, s->loc.line);
    emitByte(c, 0, s->loc.line);
}

// -----------------------------------------------------------------------------
// DECLARATIONS
// -----------------------------------------------------------------------------

static void compileFormalArg(Compiler* c, const JStarFormalArg* arg, int argIdx) {
    switch(arg->type) {
    case SIMPLE: {
        Variable var = declareVar(c, arg->as.simple, false, arg->loc);
        defineVar(c, &var, arg->loc);
        break;
    }
    case UNPACK: {
        JStarIdentifier id = createSyntheticIdentifier(c, UNPACK_ARG_FMT, argIdx);
        Variable var = declareVar(c, id, false, arg->loc);
        defineVar(c, &var, arg->loc);
        break;
    }
    }
}

static void compileFormalArgs(Compiler* c, JStarFormalArgs args) {
    int argIdx = 0;
    arrayForeach(JStarFormalArg, arg, &args) {
        compileFormalArg(c, arg, argIdx++);
    }
}

static void unpackFormalArgs(Compiler* c, JStarFormalArgs args, JStarLoc loc) {
    int argIdx = 0;
    arrayForeach(JStarFormalArg, arg, &args) {
        if(arg->type == SIMPLE) {
            continue;
        }

        char name[sizeof(UNPACK_ARG_FMT) + STRLEN_FOR_INT(int)];
        sprintf(name, UNPACK_ARG_FMT, argIdx);
        JStarIdentifier id = createIdentifier(name);

        compileVarLit(c, id, false, loc);
        emitOpcode(c, OP_UNPACK, loc.line);
        emitByte(c, arg->as.unpack.count, loc.line);

        arrayForeach(JStarIdentifier, id, &arg->as.unpack) {
            Variable unpackedArg = declareVar(c, *id, false, loc);
            defineVar(c, &unpackedArg, loc);
        }

        argIdx++;
    }
}

static ObjFunction* function(Compiler* c, ObjModule* mod, ObjString* name, const JStarStmt* s) {
    const JStarExprs* defaults = &s->as.decl.as.fun.formalArgs.defaults;
    size_t arity = s->as.decl.as.fun.formalArgs.args.count;
    JStarIdentifier vargId = s->as.decl.as.fun.formalArgs.vararg;
    bool isVararg = vargId.name != NULL;

    c->func = newFunction(c->vm, mod, name, arity, defaults->count, isVararg);
    addFunctionDefaults(c, &c->func->proto, defaults);

    // The receiver:
    //  - In the case of functions the receiver is the function itself.
    //  - In the case of methods the receiver is the class instance on which the method was called.
    Variable receiver = declareVar(c, createIdentifier(THIS_STR), false, s->loc);
    defineVar(c, &receiver, s->loc);

    compileFormalArgs(c, s->as.decl.as.fun.formalArgs.args);

    if(isVararg) {
        Variable varg = declareVar(c, vargId, false, s->loc);
        defineVar(c, &varg, s->loc);
    }

    if(s->as.decl.as.fun.isGenerator) {
        emitOpcode(c, OP_GENERATOR, s->loc.line);
    }

    unpackFormalArgs(c, s->as.decl.as.fun.formalArgs.args, s->loc);

    JStarStmt* body = s->as.decl.as.fun.body;
    compileStatements(c, &body->as.blockStmt.stmts);

    switch(c->type) {
    case TYPE_FUNC:
    case TYPE_METHOD:
        emitOpcode(c, OP_NULL, s->loc.line);
        if(s->as.decl.as.fun.isGenerator) {
            emitOpcode(c, OP_GENERATOR_CLOSE, s->loc.line);
        }
        break;
    case TYPE_CTOR:
        emitOpcode(c, OP_GET_LOCAL, s->loc.line);
        emitByte(c, 0, s->loc.line);
        break;
    }

    emitOpcode(c, OP_RETURN, s->loc.line);
    return c->func;
}

static ObjNative* native(Compiler* c, ObjString* name, const JStarStmt* s) {
    const JStarExprs* defaults = &s->as.decl.as.fun.formalArgs.defaults;
    size_t arity = s->as.decl.as.fun.formalArgs.args.count;
    JStarIdentifier vargId = s->as.decl.as.fun.formalArgs.vararg;
    bool isVararg = vargId.name != NULL;

    ObjNative* native = newNative(c->vm, c->func->proto.module, name, arity, defaults->count,
                                  isVararg, NULL);

    // Push the native on the stack in case `addFunctionDefaults` triggers a collection
    push(c->vm, OBJ_VAL(native));
    addFunctionDefaults(c, &native->proto, defaults);
    pop(c->vm);

    return native;
}

static void emitClosure(Compiler* c, ObjFunction* fn, Upvalue* upvalues, JStarLoc loc) {
    emitOpcode(c, OP_CLOSURE, loc.line);
    emitShort(c, createConst(c, OBJ_VAL(fn), loc), loc.line);
    for(uint8_t i = 0; i < fn->upvalueCount; i++) {
        emitByte(c, upvalues[i].isLocal ? 1 : 0, loc.line);
        emitByte(c, upvalues[i].index, loc.line);
    }
}

static void compileFunction(Compiler* c, FuncType type, ObjString* name, const JStarStmt* fn) {
    Compiler newCompiler;
    initCompiler(&newCompiler, c->vm, c, c->file, type, fn, c->module, c->globals, c->fwdRefs);

    enterFunctionScope(&newCompiler);
    ObjFunction* func = function(&newCompiler, c->func->proto.module, name, fn);
    exitFunctionScope(&newCompiler);

    emitClosure(c, func, newCompiler.upvalues, fn->loc);
    endCompiler(&newCompiler);
}

static uint16_t compileNative(Compiler* c, ObjString* name, Opcode nativeOp, const JStarStmt* s) {
    JSR_ASSERT(nativeOp == OP_NATIVE || nativeOp == OP_NATIVE_METHOD, "Not a native opcode");

    ObjNative* nat = native(c, name, s);
    JStarIdentifier nativeId = s->as.decl.as.native.id;
    uint16_t nativeConst = createConst(c, OBJ_VAL(nat), s->loc);

    emitOpcode(c, nativeOp, s->loc.line);
    emitShort(c, identifierConst(c, nativeId, s->loc), s->loc.line);
    emitShort(c, nativeConst, s->loc.line);

    return nativeConst;
}

static void compileDecorators(Compiler* c, const JStarExprs* decorators) {
    arrayForeach(JStarExpr*, it, decorators) {
        compileExpr(c, *it);
    }
}

static void callDecorators(Compiler* c, const JStarExprs* decorators) {
    arrayForeach(JStarExpr*, it, decorators) {
        JStarExpr* decorator = *it;
        emitOpcode(c, OP_CALL_1, decorator->loc.line);
    }
}

static ObjString* createMethodName(Compiler* c, JStarIdentifier clsName, JStarIdentifier methName) {
    size_t length = clsName.length + methName.length + 1;
    ObjString* name = allocateString(c->vm, length);
    memcpy(name->data, clsName.name, clsName.length);
    name->data[clsName.length] = '.';
    memcpy(name->data + clsName.length + 1, methName.name, methName.length);
    return name;
}

static void compileMethod(Compiler* c, const JStarStmt* cls, const JStarStmt* s) {
    FuncType type = TYPE_METHOD;
    JStarIdentifier clsId = cls->as.decl.as.cls.id;
    JStarIdentifier methId = s->as.decl.as.fun.id;

    JStarIdentifier ctorId = createIdentifier(JSR_CONSTRUCT);
    if(jsrIdentifierEq(methId, ctorId)) {
        type = TYPE_CTOR;
    }

    const JStarExprs* decorators = &s->as.decl.decorators;
    compileDecorators(c, decorators);
    compileFunction(c, type, createMethodName(c, clsId, methId), s);
    callDecorators(c, decorators);

    emitOpcode(c, OP_DEF_METHOD, cls->loc.line);
    emitShort(c, identifierConst(c, methId, s->loc), cls->loc.line);
}

static void compileNativeMethod(Compiler* c, const JStarStmt* cls, const JStarStmt* s) {
    ObjString* name = createMethodName(c, cls->as.decl.as.cls.id, s->as.decl.as.fun.id);
    uint8_t nativeConst = compileNative(c, name, OP_NATIVE_METHOD, s);

    const JStarExprs* decorators = &s->as.decl.decorators;
    if(decorators->count > 0) {
        emitOpcode(c, OP_POP, cls->loc.line);

        compileDecorators(c, decorators);

        emitOpcode(c, OP_GET_CONST, cls->loc.line);
        emitShort(c, nativeConst, s->loc.line);

        callDecorators(c, decorators);
    }

    emitOpcode(c, OP_DEF_METHOD, cls->loc.line);
    emitShort(c, identifierConst(c, s->as.decl.as.native.id, s->loc), cls->loc.line);
}

static void compileMethods(Compiler* c, const JStarStmt* s) {
    arrayForeach(JStarStmt*, it, &s->as.decl.as.cls.methods) {
        JStarStmt* method = *it;
        switch(method->type) {
        case JSR_FUNCDECL:
            compileMethod(c, s, method);
            break;
        case JSR_NATIVEDECL:
            compileNativeMethod(c, s, method);
            break;
        default:
            JSR_UNREACHABLE();
        }
    }
}

static void compileClassDecl(Compiler* c, const JStarStmt* s) {
    emitOpcode(c, OP_NEW_CLASS, s->loc.line);
    emitShort(c, identifierConst(c, s->as.decl.as.cls.id, s->loc), s->loc.line);

    Variable clsVar = declareVar(c, s->as.decl.as.cls.id, s->as.decl.isStatic, s->loc);
    defineVar(c, &clsVar, s->loc);

    enterScope(c);

    JStarIdentifier superId = createIdentifier("super");
    Variable superVar = declareVar(c, superId, false, s->loc);
    defineVar(c, &superVar, s->loc);

    if(!s->as.decl.as.cls.sup) {
        emitOpcode(c, OP_GET_OBJECT, s->loc.line);
    } else {
        compileExpr(c, s->as.decl.as.cls.sup);
    }

    compileVarLit(c, s->as.decl.as.cls.id, false, s->loc);
    emitOpcode(c, OP_SUBCLASS, s->loc.line);

    compileMethods(c, s);

    emitOpcode(c, OP_POP, s->loc.line);
    exitScope(c, s->loc.line);

    const JStarExprs* decorators = &s->as.decl.decorators;
    if(decorators->count > 0) {
        compileDecorators(c, decorators);
        compileVarLit(c, s->as.decl.as.cls.id, false, s->loc);
        callDecorators(c, decorators);

        compileVarLit(c, s->as.decl.as.cls.id, true, s->loc);
        emitOpcode(c, OP_POP, s->loc.line);
    }
}

static void compileFunDecl(Compiler* c, const JStarStmt* s) {
    JStarIdentifier funId = s->as.decl.as.fun.id;
    Variable funVar = declareVar(c, funId, s->as.decl.isStatic, s->loc);

    // If local initialize the variable in order to permit the function to reference itself
    if(funVar.scope == VAR_LOCAL) {
        initializeVar(c, &funVar);
    }

    const JStarExprs* decorators = &s->as.decl.decorators;
    compileDecorators(c, decorators);
    compileFunction(c, TYPE_FUNC, copyString(c->vm, funId.name, funId.length), s);
    callDecorators(c, decorators);

    defineVar(c, &funVar, s->loc);
}

static void compileNativeDecl(Compiler* c, const JStarStmt* s) {
    JStarIdentifier nativeId = s->as.decl.as.native.id;
    Variable natVar = declareVar(c, nativeId, s->as.decl.isStatic, s->loc);

    const JStarExprs* decorators = &s->as.decl.decorators;
    compileDecorators(c, decorators);
    compileNative(c, copyString(c->vm, nativeId.name, nativeId.length), OP_NATIVE, s);
    callDecorators(c, decorators);

    defineVar(c, &natVar, s->loc);
}

static void compileVarDecl(Compiler* c, const JStarStmt* s) {
    int varsCount = 0;
    Variable vars[MAX_LOCALS];

    arrayForeach(JStarIdentifier, varId, &s->as.decl.as.var.ids) {
        Variable var = declareVar(c, *varId, s->as.decl.isStatic, s->loc);
        if(varsCount == MAX_LOCALS) break;
        vars[varsCount++] = var;
    }

    const JStarExprs* decorators = &s->as.decl.decorators;
    if(s->as.decl.as.var.isUnpack && decorators->count > 0) {
        error(c, decorators->items[0]->loc, "Unpacking declaration cannot be decorated");
    }

    compileDecorators(c, decorators);

    if(s->as.decl.as.var.init != NULL) {
        JStarExpr* init = s->as.decl.as.var.init;
        bool isUnpack = s->as.decl.as.var.isUnpack;

        // Optimize constant unpacks by omitting tuple allocation
        if(isUnpack && IS_CONST_UNPACK(init->type)) {
            JStarExpr* exprs = getExpressions(init);
            compileConstUnpack(c, exprs, varsCount, &s->as.decl.as.var.ids);
        } else {
            compileRval(c, init, s->as.decl.as.var.ids.items[0]);
            if(isUnpack) {
                emitOpcode(c, OP_UNPACK, s->loc.line);
                emitByte(c, (uint8_t)varsCount, s->loc.line);
                adjustStackUsage(c, varsCount - 1);
            }
        }
    } else {
        // Default initialize the variables to null
        for(int i = 0; i < varsCount; i++) {
            emitOpcode(c, OP_NULL, s->loc.line);
        }
    }

    callDecorators(c, decorators);

    // define in reverse order in order to assign correct
    // values to variables in case of a const unpack
    for(int i = varsCount - 1; i >= 0; i--) {
        defineVar(c, &vars[i], s->loc);
    }
}

// Compiles a generic statement
static void compileStatement(Compiler* c, const JStarStmt* s) {
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
        exitScope(c, s->loc.line);
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
        emitOpcode(c, OP_POP, s->loc.line);
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
        JSR_UNREACHABLE();
    }
}

static void resolveFwdRefs(Compiler* c) {
    arrayForeach(FwdRef, fwdRef, c->fwdRefs) {
        if(!resolveGlobal(c, fwdRef->id)) {
            error(c, fwdRef->loc, "Cannot resolve name `%.*s`", fwdRef->id.length, fwdRef->id.name);
        }
    }
}

// -----------------------------------------------------------------------------
// API
// -----------------------------------------------------------------------------

ObjFunction* compile(JStarVM* vm, const char* filename, ObjModule* module, const JStarStmt* ast) {
    PROFILE_FUNC()

    JStarIdentifiers globals = {0};
    FwdRefs fwdRefs = {0};

    Compiler c;
    initCompiler(&c, vm, NULL, filename, TYPE_FUNC, ast, module, &globals, &fwdRefs);
    ObjFunction* func = function(&c, module, copyCString(vm, "<main>"), ast);
    resolveFwdRefs(&c);
    endCompiler(&c);

    arrayFree(vm, &fwdRefs);
    arrayFree(vm, &globals);
    return c.hadError ? NULL : func;
}

void reachCompilerRoots(JStarVM* vm, Compiler* c) {
    PROFILE_FUNC()

    while(c != NULL) {
        reachObject(vm, (Obj*)c->func);
        c = c->prev;
    }
}
