#include "compiler.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "code.h"
#include "gc.h"
#include "int_hashtable.h"
#include "jstar.h"
#include "jstar_limits.h"
#include "lib/core/core.h"
#include "object.h"
#include "opcode.h"
#include "parse/ast.h"
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
#define ANON_FMT       "anonymous[line:%d]"
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
    int line;
} FwdRef;

typedef enum FuncType {
    TYPE_FUNC,
    TYPE_METHOD,
    TYPE_CTOR,
} FuncType;

struct Compiler {
    JStarVM* vm;
    ObjModule* module;

    const char* file;

    int depth;
    Compiler* prev;

    Loop* loops;

    FuncType type;
    ObjFunction* func;
    const JStarStmt* ast;

    uint8_t localsCount;
    Local locals[MAX_LOCALS];
    Upvalue upvalues[MAX_LOCALS];

    ext_vector(char*) syntheticNames;
    ext_vector(JStarIdentifier)* globals;
    ext_vector(FwdRef)* fwdRefs;

    int stackUsage;

    int tryDepth;
    TryBlock* tryBlocks;

    bool hadError;
};

static void initCompiler(Compiler* c, JStarVM* vm, Compiler* prev, ObjModule* module,
                         const char* file, FuncType type, ext_vector(JStarIdentifier)* globals,
                         ext_vector(FwdRef)* fwdRefs, const JStarStmt* ast) {
    vm->currCompiler = c;
    *c = (Compiler){
        .vm = vm,
        .module = module,
        .file = file,
        .prev = prev,
        .type = type,
        .globals = globals,
        .fwdRefs = fwdRefs,
        .ast = ast,
        // 1 for the receiver (always present)
        .stackUsage = 1,
    };
}

static void endCompiler(Compiler* c) {
    ext_vec_foreach(char** it, c->syntheticNames) {
        free(*it);
    }
    ext_vec_free(c->syntheticNames);

    if(c->prev != NULL) {
        c->prev->hadError |= c->hadError;
    }

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

static void adjustStackUsage(Compiler* c, int num) {
    c->stackUsage += num;
    if(c->stackUsage > c->func->stackUsage) {
        c->func->stackUsage = c->stackUsage;
    }
}

static int correctLineNumber(Compiler* c, int line) {
    if(line == 0 && c->func->code.lineSize > 0) {
        return c->func->code.lines[c->func->code.lineSize - 1];
    }
    return line;
}

static size_t emitOpcode(Compiler* c, Opcode op, int line) {
    int correctedLine = correctLineNumber(c, line);
    adjustStackUsage(c, opcodeStackUsage(op));
    return writeByte(&c->func->code, op, correctedLine);
}

static size_t emitByte(Compiler* c, uint8_t b, int line) {
    int correctedLine = correctLineNumber(c, line);
    return writeByte(&c->func->code, b, correctedLine);
}

static size_t emitShort(Compiler* c, uint16_t s, int line) {
    int correctedLine = correctLineNumber(c, line);
    size_t addr = emitByte(c, (s >> 8) & 0xff, correctedLine);
    emitByte(c, s & 0xff, correctedLine);
    return addr;
}

static size_t getCurrentAddr(Compiler* c) {
    return c->func->code.size;
}

static bool inGlobalScope(Compiler* c) {
    return c->depth == 0;
}

static void discardLocal(Compiler* c, const Local* local) {
    if(local->isUpvalue) {
        emitByte(c, OP_CLOSE_UPVALUE, 0);
    } else {
        emitByte(c, OP_POP, 0);
    }
}

static void enterScope(Compiler* c) {
    c->depth++;
}

static int discardScopes(Compiler* c, int depth) {
    int locals = c->localsCount;
    while(locals > 0 && c->locals[locals - 1].depth > depth) {
        locals--;
    }

    int toPop = c->localsCount - locals;

    if(toPop > 1) {
        emitByte(c, OP_POPN, 0);
        emitByte(c, toPop, 0);
    } else if(toPop == 1) {
        discardLocal(c, &c->locals[locals]);
    }

    return toPop;
}

static void exitScope(Compiler* c) {
    int popped = discardScopes(c, --c->depth);
    c->localsCount -= popped;
    c->stackUsage -= popped;
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
    ObjString* string = copyString(c->vm, str, length);
    return createConst(c, OBJ_VAL(string), line);
}

static uint16_t identifierConst(Compiler* c, const JStarIdentifier* id, int line) {
    return stringConst(c, id->name, id->length, line);
}

static uint16_t identifierSymbol(Compiler* c, const JStarIdentifier* id, int line) {
    int index = addSymbol(&c->func->code, identifierConst(c, id, line));
    if(index == -1) {
        error(c, line, "Too many symbols in function %s", c->func->proto.name->data);
        return 0;
    }
    return (uint16_t)index;
}

static int addLocal(Compiler* c, const JStarIdentifier* id, int line) {
    if(c->localsCount == MAX_LOCALS) {
        error(c, line, "Too many local variables in function %s", c->func->proto.name->data);
        return -1;
    }
    Local* local = &c->locals[c->localsCount];
    local->isUpvalue = false;
    local->depth = -1;
    local->id = *id;
    return c->localsCount++;
}

static void initializeLocal(Compiler* c, int idx) {
    // Setting the depth field signals the local as initialized
    c->locals[idx].depth = c->depth;
}

static Variable resolveLocal(Compiler* c, const JStarIdentifier* id, int line) {
    for(int i = c->localsCount - 1; i >= 0; i--) {
        Local* local = &c->locals[i];
        if(jsrIdentifierEq(&local->id, id)) {
            if(local->depth == -1) {
                error(c, line, "Cannot read local variable `%.*s` in its own initializer",
                      local->id.length, local->id.name);
                return (Variable){.scope = VAR_ERR};
            }
            return (Variable){.scope = VAR_LOCAL, .as = {.local = {.localIdx = i}}};
        }
    }
    return (Variable){.scope = VAR_ERR};
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

static Variable resolveUpvalue(Compiler* c, const JStarIdentifier* id, int line) {
    if(c->prev == NULL) {
        return (Variable){.scope = VAR_ERR};
    }

    Variable var = resolveLocal(c->prev, id, line);
    if(var.scope == VAR_LOCAL) {
        int upvalueIdx = addUpvalue(c, var.as.local.localIdx, true, line);
        c->prev->locals[var.as.local.localIdx].isUpvalue = true;
        return (Variable){.scope = VAR_UPVALUE, .as = {.upvalue = {.upvalueIdx = upvalueIdx}}};
    }

    var = resolveUpvalue(c->prev, id, line);
    if(var.scope == VAR_UPVALUE) {
        int upvalueIdx = addUpvalue(c, var.as.upvalue.upvalueIdx, false, line);
        return (Variable){.scope = VAR_UPVALUE, .as = {.upvalue = {.upvalueIdx = upvalueIdx}}};
    }

    return var;
}

static Variable resolveGlobal(Compiler* c, const JStarIdentifier* id) {
    Variable global = (Variable){.scope = VAR_GLOBAL, .as = {.global = {.id = *id}}};

    if(c->module) {
        if(hashTableIntGetString(&c->module->globalNames, id->name, id->length,
                                 hashBytes(id->name, id->length))) {
            return global;
        }
    } else if(resolveCoreSymbol(id)) {
        return global;
    }

    ext_vec_foreach(const JStarIdentifier* globalId, *c->globals) {
        if(jsrIdentifierEq(id, globalId)) {
            return global;
        }
    }

    return (Variable){.scope = VAR_ERR};
}

static Variable resolveVar(Compiler* c, const JStarIdentifier* id, int line) {
    Variable var = resolveLocal(c, id, line);
    if(var.scope != VAR_ERR) {
        return var;
    }

    var = resolveUpvalue(c, id, line);
    if(var.scope != VAR_ERR) {
        return var;
    }

    var = resolveGlobal(c, id);
    if(var.scope != VAR_ERR) {
        return var;
    }

    if(inGlobalScope(c)) {
        return (Variable){.scope = VAR_ERR};
    }

    FwdRef fwdRef = {*id, line};
    ext_vec_push_back(*c->fwdRefs, fwdRef);

    return (Variable){.scope = VAR_GLOBAL, .as = {.global = {.id = *id}}};
}

static void initializeVar(Compiler* c, const Variable* var) {
    JSR_ASSERT(var->scope == VAR_LOCAL, "Only local variables can be marked initialized");
    initializeLocal(c, var->as.local.localIdx);
}

static Variable declareGlobal(Compiler* c, const JStarIdentifier* id) {
    ext_vec_push_back(*c->globals, *id);
    return (Variable){.scope = VAR_GLOBAL, .as = {.global = {.id = *id}}};
}

static Variable declareVar(Compiler* c, const JStarIdentifier* id, bool forceLocal, int line) {
    if(inGlobalScope(c) && !forceLocal) {
        return declareGlobal(c, id);
    }

    if(!inGlobalScope(c) && forceLocal) {
        error(c, line, "static declaration can only appear in global scope");
        return (Variable){.scope = VAR_ERR};
    }

    for(int i = c->localsCount - 1; i >= 0; i--) {
        if(c->locals[i].depth != -1 && c->locals[i].depth < c->depth) break;
        if(jsrIdentifierEq(&c->locals[i].id, id)) {
            error(c, line, "Variable `%.*s` already declared", id->length, id->name);
            return (Variable){.scope = VAR_ERR};
        }
    }

    int index = addLocal(c, id, line);
    if(index == -1) {
        return (Variable){.scope = VAR_ERR};
    }

    return (Variable){.scope = VAR_LOCAL, .as = {.local = {.localIdx = index}}};
}

static void defineVar(Compiler* c, const Variable* var, int line) {
    switch(var->scope) {
    case VAR_GLOBAL:
        emitOpcode(c, OP_DEFINE_GLOBAL, line);
        emitShort(c, identifierSymbol(c, &var->as.global.id, line), line);
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

static size_t emitJumpTo(Compiler* c, Opcode jmpOp, size_t target, int line) {
    assertJumpOpcode(jmpOp);

    int32_t offset = target - (getCurrentAddr(c) + opcodeArgsNumber(jmpOp) + 1);
    if(offset > INT16_MAX || offset < INT16_MIN) {
        error(c, line, "Too much code to jump over");
    }

    size_t jmpAddr = emitOpcode(c, jmpOp, line);
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
    loop->parent = c->loops;
    c->loops = loop;
}

static void patchLoopExitStmts(Compiler* c, size_t start, size_t contAddr, size_t brkAddr) {
    for(size_t i = start; i < getCurrentAddr(c); i++) {
        Opcode op = c->func->code.bytecode[i];
        if(op == OP_END) {
            c->func->code.bytecode[i] = OP_JUMP;

            // Patch jump with correct offset to break loop
            int mark = c->func->code.bytecode[i + 1];
            JSR_ASSERT(mark == CONTINUE_MARK || mark == BREAK_MARK, "Unknown loop breaking marker");

            setJumpTo(c, i, mark == CONTINUE_MARK ? contAddr : brkAddr, 0);

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

static void inlineMethodCall(Compiler* c, const char* name, int args) {
    JSR_ASSERT(args <= MAX_INLINE_ARGS, "Too many arguments for inline call");
    JStarIdentifier meth = createIdentifier(name);
    emitOpcode(c, OP_INVOKE_0 + args, 0);
    emitShort(c, identifierSymbol(c, &meth, 0), 0);
}

static void enterTryBlock(Compiler* c, TryBlock* exc, int numHandlers, int line) {
    exc->depth = c->depth;
    exc->numHandlers = numHandlers;
    exc->parent = c->tryBlocks;
    c->tryBlocks = exc;
    c->tryDepth += numHandlers;

    if(c->tryDepth > MAX_HANDLERS) {
        error(c, line, "Exceeded max number of nested exception handlers: max %d, got %d",
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
                error(c, e->line, "Invalid escape character `%c`", str[i + 1]);
            }
        } else {
            jsrBufferAppendChar(&sb, str[i]);
        }
    }

    ObjString* string = copyString(c->vm, sb.data, sb.size);
    jsrBufferFree(&sb);

    return string;
}

static void addFunctionDefaults(Compiler* c, const Prototype* proto,
                                ext_vector(JStarExpr*) defaults) {
    int i = 0;
    ext_vec_foreach(JStarExpr** it, defaults) {
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
    ext_vec_foreach(JStarExpr** it, exprs->as.exprList) {
        JStarExpr* e = *it;
        if(isSpreadExpr(e)) {
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
        emitOpcode(c, OP_ADD, e->line);
        break;
    case TOK_MINUS:
        emitOpcode(c, OP_SUB, e->line);
        break;
    case TOK_MULT:
        emitOpcode(c, OP_MUL, e->line);
        break;
    case TOK_DIV:
        emitOpcode(c, OP_DIV, e->line);
        break;
    case TOK_MOD:
        emitOpcode(c, OP_MOD, e->line);
        break;
    case TOK_AMPER:
        emitOpcode(c, OP_BAND, e->line);
        break;
    case TOK_PIPE:
        emitOpcode(c, OP_BOR, e->line);
        break;
    case TOK_TILDE:
        emitOpcode(c, OP_XOR, e->line);
        break;
    case TOK_LSHIFT:
        emitOpcode(c, OP_LSHIFT, e->line);
        break;
    case TOK_RSHIFT:
        emitOpcode(c, OP_RSHIFT, e->line);
        break;
    case TOK_EQUAL_EQUAL:
        emitOpcode(c, OP_EQ, e->line);
        break;
    case TOK_GT:
        emitOpcode(c, OP_GT, e->line);
        break;
    case TOK_GE:
        emitOpcode(c, OP_GE, e->line);
        break;
    case TOK_LT:
        emitOpcode(c, OP_LT, e->line);
        break;
    case TOK_LE:
        emitOpcode(c, OP_LE, e->line);
        break;
    case TOK_IS:
        emitOpcode(c, OP_IS, e->line);
        break;
    case TOK_BANG_EQ:
        emitOpcode(c, OP_EQ, e->line);
        emitOpcode(c, OP_NOT, e->line);
        break;
    default:
        JSR_UNREACHABLE();
    }
}

static void compileLogicExpr(Compiler* c, const JStarExpr* e) {
    compileExpr(c, e->as.binary.left);
    emitOpcode(c, OP_DUP, e->line);

    Opcode jmpOp = e->as.binary.op == TOK_AND ? OP_JUMPF : OP_JUMPT;
    size_t shortCircuit = emitOpcode(c, jmpOp, 0);
    emitShort(c, 0, 0);

    emitOpcode(c, OP_POP, e->line);
    compileExpr(c, e->as.binary.right);

    setJumpTo(c, shortCircuit, getCurrentAddr(c), e->line);
}

static void compileUnaryExpr(Compiler* c, const JStarExpr* e) {
    compileExpr(c, e->as.unary.operand);
    switch(e->as.unary.op) {
    case TOK_MINUS:
        emitOpcode(c, OP_NEG, e->line);
        break;
    case TOK_BANG:
        emitOpcode(c, OP_NOT, e->line);
        break;
    case TOK_TILDE:
        emitOpcode(c, OP_INVERT, e->line);
        break;
    case TOK_HASH:
        inlineMethodCall(c, "__len__", 0);
        break;
    case TOK_HASH_HASH:
        inlineMethodCall(c, "__string__", 0);
        break;
    default:
        JSR_UNREACHABLE();
    }
}

static void compileTernaryExpr(Compiler* c, const JStarExpr* e) {
    compileExpr(c, e->as.ternary.cond);

    size_t falseJmp = emitOpcode(c, OP_JUMPF, e->line);
    emitShort(c, 0, 0);

    compileExpr(c, e->as.ternary.thenExpr);
    size_t exitJmp = emitOpcode(c, OP_JUMP, e->line);
    emitShort(c, 0, 0);

    setJumpTo(c, falseJmp, getCurrentAddr(c), e->line);
    compileExpr(c, e->as.ternary.elseExpr);

    setJumpTo(c, exitJmp, getCurrentAddr(c), e->line);
}

static void compileVarLit(Compiler* c, const JStarIdentifier* id, bool set, int line) {
    Variable var = resolveVar(c, id, line);
    switch(var.scope) {
    case VAR_LOCAL:
        if(set) {
            emitOpcode(c, OP_SET_LOCAL, line);
        } else {
            emitOpcode(c, OP_GET_LOCAL, line);
        }
        emitByte(c, var.as.local.localIdx, line);
        break;
    case VAR_UPVALUE:
        if(set) {
            emitOpcode(c, OP_SET_UPVALUE, line);
        } else {
            emitOpcode(c, OP_GET_UPVALUE, line);
        }
        emitByte(c, var.as.upvalue.upvalueIdx, line);
        break;
    case VAR_GLOBAL:
        if(set) {
            emitOpcode(c, OP_SET_GLOBAL, line);
        } else {
            emitOpcode(c, OP_GET_GLOBAL, line);
        }
        emitShort(c, identifierSymbol(c, &var.as.global.id, line), line);
        break;
    case VAR_ERR:
        error(c, line, "Cannot resolve name `%.*s`", id->length, id->name);
        break;
    }
}

static void compileFunLiteral(Compiler* c, const JStarExpr* e, const JStarIdentifier* name) {
    JStarStmt* func = e->as.funLit.func;
    if(!name) {
        char anonName[sizeof(ANON_FMT) + STRLEN_FOR_INT(int) + 1];
        sprintf(anonName, ANON_FMT, func->line);
        compileFunction(c, TYPE_FUNC, copyString(c->vm, anonName, strlen(anonName)), func);
    } else {
        func->as.decl.as.fun.id = *name;
        compileFunction(c, TYPE_FUNC, copyString(c->vm, name->name, name->length), func);
    }
}

static void compileLval(Compiler* c, const JStarExpr* e) {
    switch(e->type) {
    case JSR_VAR:
        compileVarLit(c, &e->as.varLiteral.id, true, e->line);
        break;
    case JSR_PROPERTY_ACCESS: {
        compileExpr(c, e->as.propertyAccess.left);
        emitOpcode(c, OP_SET_FIELD, e->line);
        emitShort(c, identifierSymbol(c, &e->as.propertyAccess.id, e->line), e->line);
        break;
    }
    case JSR_INDEX: {
        compileExpr(c, e->as.index.index);
        compileExpr(c, e->as.index.left);
        emitOpcode(c, OP_SUBSCR_SET, e->line);
        break;
    }
    default:
        JSR_UNREACHABLE();
    }
}

// The `name` argument is the name of the variable to which we are assigning to.
// In case of a function literal we use it to give the function a meaningful name, instead
// of just using the default name for anonymous functions (that is: `anon:<line_number>`)
static void compileRval(Compiler* c, const JStarExpr* e, const JStarIdentifier* name) {
    if(e->type == JSR_FUN_LIT) {
        compileFunLiteral(c, e, name);
    } else {
        compileExpr(c, e);
    }
}

static void compileConstUnpack(Compiler* c, const JStarExpr* exprs, int lvals,
                               const ext_vector(JStarIdentifier) names) {
    if(ext_vec_size(exprs->as.exprList) < (size_t)lvals) {
        error(c, exprs->line, "Too few values to unpack: expected %d, got %zu", lvals,
              ext_vec_size(exprs->as.exprList));
    }

    int i = 0;
    ext_vec_foreach(JStarExpr** it, exprs->as.exprList) {
        const JStarIdentifier* name = NULL;
        if(names && i < lvals) {
            name = &names[i];
        }

        compileRval(c, *it, name);

        if(++i > lvals) {
            emitOpcode(c, OP_POP, 0);
        }
    }
}

// Compile an unpack assignment of the form: a, b, ..., z = ...
static void compileUnpackAssign(Compiler* c, const JStarExpr* e) {
    JStarExpr* lvals = e->as.assign.lval->as.tupleLiteral.exprs;
    size_t lvalCount = ext_vec_size(lvals->as.exprList);

    if(lvalCount >= UINT8_MAX) {
        error(c, e->line, "Exceeded max number of unpack assignment: %d", UINT8_MAX);
    }

    JStarExpr* rval = e->as.assign.rval;

    // Optimize constant unpacks by omitting tuple allocation
    if(IS_CONST_UNPACK(rval->type)) {
        JStarExpr* exprs = getExpressions(rval);
        compileConstUnpack(c, exprs, lvalCount, NULL);
    } else {
        compileRval(c, rval, NULL);
        emitOpcode(c, OP_UNPACK, e->line);
        emitByte(c, (uint8_t)lvalCount, e->line);
        adjustStackUsage(c, lvalCount - 1);
    }

    // compile lvals in reverse order in order to assign
    // correct values to variables in case of a const unpack
    for(int n = lvalCount - 1; n >= 0; n--) {
        JStarExpr* lval = lvals->as.exprList[n];
        compileLval(c, lval);
        if(n != 0) emitOpcode(c, OP_POP, e->line);
    }
}

static void compileAssignExpr(Compiler* c, const JStarExpr* e) {
    switch(e->as.assign.lval->type) {
    case JSR_VAR: {
        JStarIdentifier* name = &e->as.assign.lval->as.varLiteral.id;
        compileRval(c, e->as.assign.rval, name);
        compileLval(c, e->as.assign.lval);
        break;
    }
    case JSR_PROPERTY_ACCESS: {
        JStarIdentifier* name = &e->as.assign.lval->as.propertyAccess.id;
        compileRval(c, e->as.assign.rval, name);
        compileLval(c, e->as.assign.lval);
        break;
    }
    case JSR_INDEX: {
        compileRval(c, e->as.assign.rval, NULL);
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
    JStarExpr binary = {e->line, JSR_BINARY, .as = {.binary = {op, l, r}}};
    JStarExpr assignment = {e->line, JSR_ASSIGN, .as = {.assign = {l, &binary}}};

    // compile as a normal assignment
    compileAssignExpr(c, &assignment);
}

static void compileArguments(Compiler* c, const JStarExpr* args) {
    ext_vec_foreach(JStarExpr** it, args->as.exprList) {
        compileExpr(c, *it);
    }

    size_t argsCount = ext_vec_size(args->as.exprList);
    if(argsCount >= UINT8_MAX) {
        error(c, args->line, "Exceeded maximum number of arguments (%d) for function %s",
              (int)UINT8_MAX, c->func->proto.name->data);
    }
}

static void compileUnpackArguments(Compiler* c, JStarExpr* args) {
    JStarExpr argsList = (JStarExpr){args->line, JSR_LIST, .as = {.listLiteral = {args}}};
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

    uint8_t argsCount = ext_vec_size(args->as.exprList);
    emitCallOp(c, callCode, callInline, callUnpack, argsCount, unpackCall, e->line);

    if(isMethod) {
        emitShort(c, identifierSymbol(c, &callee->as.propertyAccess.id, e->line), e->line);
    }
}

static void compileSuper(Compiler* c, const JStarExpr* e) {
    // TODO: replace check to support super calls in closures nested in methods
    if(c->type != TYPE_METHOD && c->type != TYPE_CTOR) {
        error(c, e->line, "Can only use `super` in method call");
        return;
    }

    // place `this` on top of the stack
    emitOpcode(c, OP_GET_LOCAL, e->line);
    emitByte(c, 0, e->line);

    uint16_t methodSym;
    if(e->as.sup.name.name != NULL) {
        methodSym = identifierSymbol(c, &e->as.sup.name, e->line);
    } else {
        methodSym = identifierSymbol(c, &c->ast->as.decl.as.fun.id, e->line);
    }

    JStarIdentifier superName = createIdentifier("super");
    JStarExpr* args = e->as.sup.args;

    if(args != NULL) {
        bool unpackCall = containsSpreadExpr(args);

        if(unpackCall) {
            compileUnpackArguments(c, args);
        } else {
            compileArguments(c, args);
        }

        uint8_t argsCount = ext_vec_size(args->as.exprList);
        compileVarLit(c, &superName, false, e->line);
        emitCallOp(c, OP_SUPER, OP_SUPER_0, OP_SUPER_UNPACK, argsCount, unpackCall, e->line);
        emitShort(c, methodSym, e->line);
    } else {
        compileVarLit(c, &superName, false, e->line);
        emitOpcode(c, OP_SUPER_BIND, e->line);
        emitShort(c, methodSym, e->line);
    }
}

static void compileAccessExpression(Compiler* c, const JStarExpr* e) {
    compileExpr(c, e->as.propertyAccess.left);
    emitOpcode(c, OP_GET_FIELD, e->line);
    emitShort(c, identifierSymbol(c, &e->as.propertyAccess.id, e->line), e->line);
}

static void compileArraryAccExpression(Compiler* c, const JStarExpr* e) {
    compileExpr(c, e->as.index.left);
    compileExpr(c, e->as.index.index);
    emitOpcode(c, OP_SUBSCR_GET, e->line);
}

static void compilePowExpr(Compiler* c, const JStarExpr* e) {
    compileExpr(c, e->as.pow.base);
    compileExpr(c, e->as.pow.exp);
    emitOpcode(c, OP_POW, e->line);
}

static void compileListLit(Compiler* c, const JStarExpr* e) {
    emitOpcode(c, OP_NEW_LIST, e->line);

    ext_vec_foreach(JStarExpr** it, e->as.listLiteral.exprs->as.exprList) {
        JStarExpr* expr = *it;

        if(isSpreadExpr(expr)) {
            emitOpcode(c, OP_DUP, e->line);
        }

        compileExpr(c, expr);

        if(isSpreadExpr(expr)) {
            inlineMethodCall(c, "addAll", 1);
            emitOpcode(c, OP_POP, expr->line);
        } else {
            emitOpcode(c, OP_APPEND_LIST, e->line);
        }
    }
}

static void compileSpreadTupleLit(Compiler* c, const JStarExpr* e) {
    JStarExpr toList = (JStarExpr){e->line, JSR_LIST,
                                   .as = {.listLiteral = {e->as.tupleLiteral.exprs}}};
    compileListLit(c, &toList);
    emitOpcode(c, OP_LIST_TO_TUPLE, e->line);
}

static void compileTupleLit(Compiler* c, const JStarExpr* e) {
    if(containsSpreadExpr(e->as.tupleLiteral.exprs)) {
        compileSpreadTupleLit(c, e);
        return;
    }

    ext_vec_foreach(JStarExpr** it, e->as.tupleLiteral.exprs->as.exprList) {
        compileExpr(c, *it);
    }

    size_t tupleSize = ext_vec_size(e->as.tupleLiteral.exprs->as.exprList);
    if(tupleSize >= UINT8_MAX) {
        error(c, e->line, "Too many elements in Tuple literal");
    }

    emitOpcode(c, OP_NEW_TUPLE, e->line);
    emitByte(c, tupleSize, e->line);
}

static void compileTableLit(Compiler* c, const JStarExpr* e) {
    emitOpcode(c, OP_NEW_TABLE, e->line);

    JStarExpr* keyVals = e->as.tableLiteral.keyVals;
    for(JStarExpr** it = ext_vec_begin(keyVals->as.exprList);
        it != ext_vec_end(keyVals->as.exprList);) {
        JStarExpr* expr = *it;

        if(isSpreadExpr(expr)) {
            emitOpcode(c, OP_DUP, e->line);
            compileExpr(c, expr);
            inlineMethodCall(c, "addAll", 1);
            emitOpcode(c, OP_POP, e->line);

            it += 1;
        } else {
            const JStarExpr* key = expr;
            const JStarExpr* val = *(it + 1);

            emitOpcode(c, OP_DUP, e->line);
            compileExpr(c, key);
            compileExpr(c, val);

            inlineMethodCall(c, "__set__", 2);
            emitOpcode(c, OP_POP, e->line);

            it += 2;
        }
    }
}

static void compileYield(Compiler* c, const JStarExpr* e) {
    if(c->type == TYPE_CTOR) {
        error(c, e->line, "Cannot use yield in constructor");
    }

    if(e->as.yield.expr != NULL) {
        compileExpr(c, e->as.yield.expr);
    } else {
        emitOpcode(c, OP_NULL, e->line);
    }

    emitOpcode(c, OP_YIELD, e->line);
}

static void emitValueConst(Compiler* c, Value val, int line) {
    emitOpcode(c, OP_GET_CONST, line);
    emitShort(c, createConst(c, val, line), line);
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
        emitValueConst(c, NUM_VAL(e->as.num), e->line);
        break;
    case JSR_BOOL:
        emitValueConst(c, BOOL_VAL(e->as.boolean), e->line);
        break;
    case JSR_STRING:
        emitValueConst(c, OBJ_VAL(readString(c, e)), e->line);
        break;
    case JSR_VAR:
        compileVarLit(c, &e->as.varLiteral.id, false, e->line);
        break;
    case JSR_NULL:
        emitOpcode(c, OP_NULL, e->line);
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
        compileFunLiteral(c, e, NULL);
        break;
    case JSR_SPREAD:
        compileExpr(c, e->as.spread.expr);
        break;
    case JSR_EXPR_LST:
        ext_vec_foreach(JStarExpr** it, e->as.exprList) {
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

static void compileStatements(Compiler* c, ext_vector(JStarStmt*) stmts) {
    ext_vec_foreach(JStarStmt** it, stmts) {
        compileStatement(c, *it);
    }
}

static void compileReturnStatement(Compiler* c, const JStarStmt* s) {
    if(c->type == TYPE_CTOR) {
        error(c, s->line, "Cannot use return in constructor");
    }

    if(s->as.returnStmt.e != NULL) {
        compileExpr(c, s->as.returnStmt.e);
    } else {
        emitOpcode(c, OP_NULL, s->line);
    }

    if(c->ast->as.decl.as.fun.isGenerator) {
        emitOpcode(c, OP_GENERATOR_CLOSE, s->line);
    }

    emitOpcode(c, OP_RETURN, s->line);
}

static void compileIfStatement(Compiler* c, const JStarStmt* s) {
    compileExpr(c, s->as.ifStmt.cond);

    size_t falseJmp = emitOpcode(c, OP_JUMPF, 0);
    emitShort(c, 0, 0);

    compileStatement(c, s->as.ifStmt.thenStmt);

    size_t exitJmp = 0;
    if(s->as.ifStmt.elseStmt != NULL) {
        exitJmp = emitOpcode(c, OP_JUMP, 0);
        emitShort(c, 0, 0);
    }

    setJumpTo(c, falseJmp, getCurrentAddr(c), s->line);

    if(s->as.ifStmt.elseStmt != NULL) {
        compileStatement(c, s->as.ifStmt.elseStmt);
        setJumpTo(c, exitJmp, getCurrentAddr(c), s->line);
    }
}

static void compileForStatement(Compiler* c, const JStarStmt* s) {
    enterScope(c);

    if(s->as.forStmt.init != NULL) {
        compileStatement(c, s->as.forStmt.init);
    }

    size_t firstJmp = 0;
    if(s->as.forStmt.act != NULL) {
        firstJmp = emitOpcode(c, OP_JUMP, 0);
        emitShort(c, 0, 0);
    }

    Loop l;
    startLoop(c, &l);

    if(s->as.forStmt.act != NULL) {
        compileExpr(c, s->as.forStmt.act);
        emitOpcode(c, OP_POP, 0);
        setJumpTo(c, firstJmp, getCurrentAddr(c), 0);
    }

    size_t exitJmp = 0;
    if(s->as.forStmt.cond != NULL) {
        compileExpr(c, s->as.forStmt.cond);
        exitJmp = emitOpcode(c, OP_JUMPF, 0);
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
static void compileForEach(Compiler* c, const JStarStmt* s) {
    enterScope(c);

    // Evaluate `iterable` once and store it in a local
    JStarIdentifier exprName = createIdentifier(".expr");
    Variable exprVar = declareVar(c, &exprName, false, s->as.forEach.iterable->line);
    defineVar(c, &exprVar, s->as.forEach.iterable->line);
    compileExpr(c, s->as.forEach.iterable);

    // Set the iterator variable with a name that it's not an identifier.
    JStarIdentifier iteratorName = createIdentifier(".iter");
    Variable iterVar = declareVar(c, &iteratorName, false, s->line);
    defineVar(c, &iterVar, s->line);
    emitOpcode(c, OP_NULL, 0);  // initialize `.iter` to null

    // FOR_PREP will cache the __iter__ and __next__ methods as locals
    emitOpcode(c, OP_FOR_PREP, 0);

    // Declare variables for cached methods
    JStarIdentifier iterMethName = createIdentifier(".__iter__");
    Variable iterMethVar = declareVar(c, &iterMethName, false, s->line);
    defineVar(c, &iterMethVar, s->line);

    JStarIdentifier nextMethName = createIdentifier(".__next__");
    Variable nextMethVar = declareVar(c, &nextMethName, false, s->line);
    defineVar(c, &nextMethVar, s->line);

    Loop l;
    startLoop(c, &l);

    emitOpcode(c, OP_FOR_ITER, s->line);
    size_t exitJmp = emitOpcode(c, OP_FOR_NEXT, 0);
    emitShort(c, 0, 0);

    JStarStmt* varDecl = s->as.forEach.var;
    enterScope(c);

    ext_vec_foreach(JStarIdentifier * name, varDecl->as.decl.as.var.ids) {
        Variable var = declareVar(c, name, false, s->line);
        defineVar(c, &var, s->line);
    }

    uint8_t numDecls = ext_vec_size(varDecl->as.decl.as.var.ids);
    if(varDecl->as.decl.as.var.isUnpack) {
        emitOpcode(c, OP_UNPACK, s->line);
        emitByte(c, numDecls, s->line);
        adjustStackUsage(c, numDecls);
    }

    JStarStmt* body = s->as.forEach.body;
    compileStatements(c, body->as.blockStmt.stmts);

    exitScope(c);

    emitJumpTo(c, OP_JUMP, l.start, s->line);
    setJumpTo(c, exitJmp, getCurrentAddr(c), s->line);

    endLoop(c);
    exitScope(c);
}

static void compileWhileStatement(Compiler* c, const JStarStmt* s) {
    Loop l;
    startLoop(c, &l);

    compileExpr(c, s->as.whileStmt.cond);
    size_t exitJmp = emitOpcode(c, OP_JUMPF, 0);
    emitShort(c, 0, 0);

    compileStatement(c, s->as.whileStmt.body);

    emitJumpTo(c, OP_JUMP, l.start, s->line);
    setJumpTo(c, exitJmp, getCurrentAddr(c), s->line);

    endLoop(c);
}

static void compileImportStatement(Compiler* c, const JStarStmt* s) {
    ext_vector(JStarIdentifier) modules = s->as.importStmt.modules;
    ext_vector(JStarIdentifier) names = s->as.importStmt.names;
    bool importFor = !ext_vec_empty(names);
    bool importAs = s->as.importStmt.as.name != NULL;

    JStarBuffer nameBuf;
    jsrBufferInit(c->vm, &nameBuf);

    Variable modVar;
    if(!importFor) {
        const JStarIdentifier* name = importAs ? &s->as.importStmt.as : &modules[0];
        modVar = declareVar(c, name, false, s->line);
    }

    ext_vec_foreach(const JStarIdentifier* submoduleName, modules) {
        jsrBufferAppend(&nameBuf, submoduleName->name, submoduleName->length);

        if(importAs && submoduleName == ext_vec_end(modules) - 1) {
            emitOpcode(c, OP_IMPORT, s->line);
        } else if(submoduleName == ext_vec_begin(modules) && !(importAs || importFor)) {
            emitOpcode(c, OP_IMPORT, s->line);
        } else {
            emitOpcode(c, OP_IMPORT_FROM, s->line);
        }

        emitShort(c, stringConst(c, nameBuf.data, nameBuf.size, s->line), s->line);
        emitOpcode(c, OP_POP, s->line);

        if(submoduleName != ext_vec_end(modules) - 1) {
            jsrBufferAppendChar(&nameBuf, '.');
        }
    }

    if(importFor) {
        ext_vec_foreach(const JStarIdentifier* name, names) {
            emitOpcode(c, OP_IMPORT_NAME, s->line);
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

static void compileExcepts(Compiler* c, ext_vector(JStarStmt*) excepts, size_t curr) {
    const JStarStmt* except = excepts[curr];

    // Retrieve the exception by using its phny variable name
    JStarIdentifier exceptionName = createIdentifier(".exception");
    compileVarLit(c, &exceptionName, false, except->line);

    // Test the exception's class
    compileExpr(c, except->as.excStmt.cls);
    emitOpcode(c, OP_IS, 0);

    size_t falseJmp = emitOpcode(c, OP_JUMPF, 0);
    emitShort(c, 0, 0);

    // Compile the handler code
    enterScope(c);
    adjustStackUsage(c, 1);

    // Retrieve the exception again, this time binding it to a local
    compileVarLit(c, &exceptionName, false, except->line);
    Variable excVar = declareVar(c, &except->as.excStmt.var, false, except->line);
    defineVar(c, &excVar, except->line);

    JStarStmt* body = except->as.excStmt.block;
    compileStatements(c, body->as.blockStmt.stmts);

    // Set the exception cause to `null` to signal that the exception has been handled
    JStarIdentifier causeName = createIdentifier(".cause");
    emitOpcode(c, OP_NULL, except->line);
    compileVarLit(c, &causeName, true, except->line);
    emitOpcode(c, OP_POP, except->line);

    adjustStackUsage(c, -1);
    exitScope(c);

    size_t exitJmp = 0;
    if(curr < ext_vec_size(excepts) - 1) {
        exitJmp = emitOpcode(c, OP_JUMP, 0);
        emitShort(c, 0, 0);
    }

    setJumpTo(c, falseJmp, getCurrentAddr(c), except->line);

    // Compile the next handler
    if(curr < ext_vec_size(excepts) - 1) {
        compileExcepts(c, excepts, curr + 1);
        setJumpTo(c, exitJmp, getCurrentAddr(c), except->line);
    }
}

static void compileTryExcept(Compiler* c, const JStarStmt* s) {
    bool hasExcepts = !ext_vec_empty(s->as.tryStmt.excs);
    bool hasEnsure = s->as.tryStmt.ensure != NULL;
    int numHandlers = (hasExcepts ? 1 : 0) + (hasEnsure ? 1 : 0);

    TryBlock tryBlock;
    enterTryBlock(c, &tryBlock, numHandlers, s->line);

    size_t ensureSetup = 0, exceptSetup = 0;

    if(hasEnsure) {
        ensureSetup = emitOpcode(c, OP_SETUP_ENSURE, s->line);
        emitShort(c, 0, 0);
    }

    if(hasExcepts) {
        exceptSetup = emitOpcode(c, OP_SETUP_EXCEPT, s->line);
        emitShort(c, 0, 0);
    }

    compileStatement(c, s->as.tryStmt.block);

    if(hasExcepts) {
        emitOpcode(c, OP_POP_HANDLER, s->line);
    }

    if(hasEnsure) {
        emitOpcode(c, OP_POP_HANDLER, s->line);
        // Reached end of try block during normal execution flow, set exception and unwind
        // cause to null to signal the ensure handler that no exception was raised
        emitOpcode(c, OP_NULL, s->line);
        emitOpcode(c, OP_NULL, s->line);
    }

    enterScope(c);

    // Phony variable for the exception
    JStarIdentifier exceptionName = createIdentifier(".exception");
    Variable excVar = declareVar(c, &exceptionName, false, 0);
    defineVar(c, &excVar, 0);

    // Phony variable for the unwing cause enum
    JStarIdentifier causeName = createIdentifier(".cause");
    Variable causeVar = declareVar(c, &causeName, false, 0);
    defineVar(c, &causeVar, 0);

    // Compile excepts (if any)
    if(hasExcepts) {
        size_t excJmp = emitOpcode(c, OP_JUMP, 0);
        emitShort(c, 0, 0);

        setJumpTo(c, exceptSetup, getCurrentAddr(c), s->line);
        compileExcepts(c, s->as.tryStmt.excs, 0);

        if(hasEnsure) {
            emitOpcode(c, OP_POP_HANDLER, 0);
        } else {
            emitOpcode(c, OP_END_HANDLER, 0);
            exitScope(c);
        }

        setJumpTo(c, excJmp, getCurrentAddr(c), 0);
    }

    // Compile ensure block (if any)
    if(hasEnsure) {
        setJumpTo(c, ensureSetup, getCurrentAddr(c), s->line);
        compileStatement(c, s->as.tryStmt.ensure);
        emitOpcode(c, OP_END_HANDLER, 0);
        exitScope(c);
    }

    exitTryBlock(c);
}

static void compileRaiseStmt(Compiler* c, const JStarStmt* s) {
    compileExpr(c, s->as.raiseStmt.exc);
    emitOpcode(c, OP_RAISE, s->line);
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
    emitOpcode(c, OP_NULL, s->line);
    Variable var = declareVar(c, &s->as.withStmt.var, false, s->line);
    defineVar(c, &var, s->line);

    // try
    TryBlock tryBlock;
    enterTryBlock(c, &tryBlock, 1, s->line);

    size_t ensSetup = emitOpcode(c, OP_SETUP_ENSURE, s->line);
    emitShort(c, 0, 0);

    // x = closable
    JStarExpr lval = {.line = s->line, .type = JSR_VAR, .as = {.varLiteral = {s->as.withStmt.var}}};
    JStarExpr assign = {.line = s->line,
                        .type = JSR_ASSIGN,
                        .as = {.assign = {.lval = &lval, .rval = s->as.withStmt.e}}};
    compileExpr(c, &assign);
    emitOpcode(c, OP_POP, s->line);

    // code
    compileStatement(c, s->as.withStmt.block);

    emitOpcode(c, OP_POP_HANDLER, s->line);
    emitOpcode(c, OP_NULL, s->line);
    emitOpcode(c, OP_NULL, s->line);

    // ensure
    enterScope(c);

    JStarIdentifier exceptionName = createIdentifier(".exception");
    Variable excVar = declareVar(c, &exceptionName, false, 0);
    defineVar(c, &excVar, 0);

    JStarIdentifier causeName = createIdentifier(".cause");
    Variable causeVar = declareVar(c, &causeName, false, 0);
    defineVar(c, &causeVar, 0);

    setJumpTo(c, ensSetup, getCurrentAddr(c), s->line);

    // if x then x.close() end
    compileVarLit(c, &s->as.withStmt.var, false, s->line);
    size_t falseJmp = emitOpcode(c, OP_JUMPF, s->line);
    emitShort(c, 0, 0);

    compileVarLit(c, &s->as.withStmt.var, false, s->line);
    inlineMethodCall(c, "close", 0);
    emitOpcode(c, OP_POP, s->line);

    setJumpTo(c, falseJmp, getCurrentAddr(c), s->line);

    emitOpcode(c, OP_END_HANDLER, 0);
    exitScope(c);

    exitTryBlock(c);
    exitScope(c);
}

static void compileLoopExitStmt(Compiler* c, const JStarStmt* s) {
    bool isBreak = s->type == JSR_BREAK;

    if(c->loops == NULL) {
        error(c, s->line, "Cannot use %s outside loop", isBreak ? "break" : "continue");
        return;
    }

    if(c->tryDepth != 0 && c->tryBlocks->depth >= c->loops->depth) {
        error(c, s->line, "Cannot %s out of a try block", isBreak ? "break" : "continue");
    }

    discardScopes(c, c->loops->depth);

    // Emit place-holder instruction that will be patched at the end of loop compilation
    // when we know the offset to emit for a break or continue jump
    emitOpcode(c, OP_END, s->line);
    emitByte(c, isBreak ? BREAK_MARK : CONTINUE_MARK, s->line);
    emitByte(c, 0, s->line);
}

// -----------------------------------------------------------------------------
// DECLARATIONS
// -----------------------------------------------------------------------------

static void compileFormalArg(Compiler* c, const JStarFormalArg* arg, int argIdx, int line) {
    switch(arg->type) {
    case SIMPLE: {
        Variable var = declareVar(c, &arg->as.simple, false, line);
        defineVar(c, &var, line);
        break;
    }
    case UNPACK: {
        char* name = malloc(sizeof(UNPACK_ARG_FMT) + STRLEN_FOR_INT(int) + 1);
        sprintf(name, UNPACK_ARG_FMT, argIdx);
        ext_vec_push_back(c->syntheticNames, name);

        JStarIdentifier id = createIdentifier(name);

        Variable var = declareVar(c, &id, false, line);
        defineVar(c, &var, line);
        break;
    }
    }
}

static void compileFormalArgs(Compiler* c, const ext_vector(JStarFormalArg) args, int line) {
    int argIdx = 0;
    ext_vec_foreach(const JStarFormalArg* arg, args) {
        compileFormalArg(c, arg, argIdx++, line);
    }
}

static void unpackFormalArgs(Compiler* c, const ext_vector(JStarFormalArg) args, int line) {
    int argIdx = 0;
    ext_vec_foreach(const JStarFormalArg* arg, args) {
        if(arg->type == SIMPLE) {
            continue;
        }

        char name[sizeof(UNPACK_ARG_FMT) + STRLEN_FOR_INT(int)];
        sprintf(name, UNPACK_ARG_FMT, argIdx);
        JStarIdentifier id = createIdentifier(name);

        compileVarLit(c, &id, false, line);
        emitOpcode(c, OP_UNPACK, line);
        emitByte(c, ext_vec_size(arg->as.unpack), line);

        ext_vec_foreach(const JStarIdentifier* id, arg->as.unpack) {
            Variable unpackedArg = declareVar(c, id, false, line);
            defineVar(c, &unpackedArg, line);
        }

        argIdx++;
    }
}

static ObjFunction* function(Compiler* c, ObjModule* m, ObjString* name, const JStarStmt* s) {
    size_t defaults = ext_vec_size(s->as.decl.as.fun.formalArgs.defaults);
    size_t arity = ext_vec_size(s->as.decl.as.fun.formalArgs.args);
    const JStarIdentifier* varargName = &s->as.decl.as.fun.formalArgs.vararg;
    bool isVararg = varargName->name != NULL;

    // Allocate a new function. We need to push `name` on the stack in case a collection happens
    push(c->vm, OBJ_VAL(name));
    c->func = newFunction(c->vm, m, arity, defaults, isVararg);
    c->func->proto.name = name;
    pop(c->vm);

    addFunctionDefaults(c, &c->func->proto, s->as.decl.as.fun.formalArgs.defaults);

    // Add the receiver.
    // In the case of functions the receiver is the function itself.
    // In the case of methods the receiver is the class instance on which the method was called.
    JStarIdentifier receiverName = createIdentifier("this");
    int receiverLocal = addLocal(c, &receiverName, s->line);
    initializeLocal(c, receiverLocal);

    compileFormalArgs(c, s->as.decl.as.fun.formalArgs.args, s->line);

    if(isVararg) {
        Variable vararg = declareVar(c, varargName, false, s->line);
        defineVar(c, &vararg, s->line);
    }

    if(s->as.decl.as.fun.isGenerator) {
        emitOpcode(c, OP_GENERATOR, s->line);
    }

    unpackFormalArgs(c, s->as.decl.as.fun.formalArgs.args, s->line);

    JStarStmt* body = s->as.decl.as.fun.body;
    compileStatements(c, body->as.blockStmt.stmts);

    switch(c->type) {
    case TYPE_FUNC:
    case TYPE_METHOD:
        emitOpcode(c, OP_NULL, 0);
        if(s->as.decl.as.fun.isGenerator) {
            emitOpcode(c, OP_GENERATOR_CLOSE, 0);
        }
        break;
    case TYPE_CTOR:
        emitOpcode(c, OP_GET_LOCAL, 0);
        emitByte(c, 0, 0);
        break;
    }

    emitOpcode(c, OP_RETURN, 0);

    return c->func;
}

static ObjNative* native(Compiler* c, ObjModule* m, ObjString* name, const JStarStmt* s) {
    size_t defaults = ext_vec_size(s->as.decl.as.fun.formalArgs.defaults);
    size_t arity = ext_vec_size(s->as.decl.as.fun.formalArgs.args);
    const JStarIdentifier* varargName = &s->as.decl.as.fun.formalArgs.vararg;
    bool isVararg = varargName->name != NULL;

    // Allocate a new native. We need to push `name` on the stack in case a collection happens
    push(c->vm, OBJ_VAL(name));
    ObjNative* native = newNative(c->vm, c->func->proto.module, arity, defaults, isVararg);
    native->proto.name = name;
    pop(c->vm);

    // Push the native on the stack in case `addFunctionDefaults` triggers a collection
    push(c->vm, OBJ_VAL(native));
    addFunctionDefaults(c, &native->proto, s->as.decl.as.native.formalArgs.defaults);
    pop(c->vm);

    return native;
}

static void emitClosure(Compiler* c, ObjFunction* fn, Upvalue* upvalues, int line) {
    emitOpcode(c, OP_CLOSURE, line);
    emitShort(c, createConst(c, OBJ_VAL(fn), line), line);
    for(uint8_t i = 0; i < fn->upvalueCount; i++) {
        emitByte(c, upvalues[i].isLocal ? 1 : 0, line);
        emitByte(c, upvalues[i].index, line);
    }
}

static void compileFunction(Compiler* c, FuncType type, ObjString* name, const JStarStmt* fn) {
    Compiler compiler;
    initCompiler(&compiler, c->vm, c, c->module, c->file, type, c->globals, c->fwdRefs, fn);

    enterFunctionScope(&compiler);
    ObjFunction* func = function(&compiler, c->func->proto.module, name, fn);
    exitFunctionScope(&compiler);

    emitClosure(c, func, compiler.upvalues, fn->line);

    endCompiler(&compiler);
}

static uint16_t compileNative(Compiler* c, ObjString* name, Opcode nativeOp, const JStarStmt* s) {
    JSR_ASSERT(nativeOp == OP_NATIVE || nativeOp == OP_NATIVE_METHOD, "Not a native opcode");

    ObjNative* nat = native(c, c->func->proto.module, name, s);
    const JStarIdentifier* nativeName = &s->as.decl.as.native.id;
    uint16_t nativeConst = createConst(c, OBJ_VAL(nat), s->line);

    emitOpcode(c, nativeOp, s->line);
    emitShort(c, identifierConst(c, nativeName, s->line), s->line);
    emitShort(c, nativeConst, s->line);

    return nativeConst;
}

static void compileDecorators(Compiler* c, ext_vector(JStarExpr*) decorators) {
    ext_vec_foreach(JStarExpr** e, decorators) {
        compileExpr(c, *e);
    }
}

static void callDecorators(Compiler* c, ext_vector(JStarExpr*) decorators) {
    ext_vec_foreach(JStarExpr** e, decorators) {
        JStarExpr* decorator = *e;
        emitOpcode(c, OP_CALL_1, decorator->line);
    }
}

static ObjString* createMethodName(Compiler* c, const JStarIdentifier* clsName,
                                   const JStarIdentifier* methName) {
    size_t length = clsName->length + methName->length + 1;
    ObjString* name = allocateString(c->vm, length);
    memcpy(name->data, clsName->name, clsName->length);
    name->data[clsName->length] = '.';
    memcpy(name->data + clsName->length + 1, methName->name, methName->length);
    return name;
}

static void compileMethod(Compiler* c, const JStarStmt* cls, const JStarStmt* s) {
    FuncType type = TYPE_METHOD;
    const JStarIdentifier* clsName = &cls->as.decl.as.cls.id;
    const JStarIdentifier* methName = &s->as.decl.as.fun.id;

    JStarIdentifier ctorName = createIdentifier(JSR_CONSTRUCT);
    if(jsrIdentifierEq(methName, &ctorName)) {
        type = TYPE_CTOR;
    }

    ext_vector(JStarExpr*) decorators = s->as.decl.decorators;

    compileDecorators(c, decorators);
    compileFunction(c, type, createMethodName(c, clsName, methName), s);
    callDecorators(c, decorators);

    emitOpcode(c, OP_DEF_METHOD, cls->line);
    emitShort(c, identifierConst(c, methName, s->line), cls->line);
}

static void compileNativeMethod(Compiler* c, const JStarStmt* cls, const JStarStmt* s) {
    ObjString* name = createMethodName(c, &cls->as.decl.as.cls.id, &s->as.decl.as.fun.id);
    uint8_t nativeConst = compileNative(c, name, OP_NATIVE_METHOD, s);

    ext_vector(JStarExpr*) decorators = s->as.decl.decorators;
    if(ext_vec_size(decorators)) {
        emitOpcode(c, OP_POP, cls->line);

        compileDecorators(c, decorators);

        emitOpcode(c, OP_GET_CONST, cls->line);
        emitShort(c, nativeConst, s->line);

        callDecorators(c, decorators);
    }

    emitOpcode(c, OP_DEF_METHOD, cls->line);
    emitShort(c, identifierConst(c, &s->as.decl.as.native.id, s->line), cls->line);
}

static void compileMethods(Compiler* c, const JStarStmt* s) {
    ext_vec_foreach(JStarStmt** it, s->as.decl.as.cls.methods) {
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
    emitOpcode(c, OP_NEW_CLASS, s->line);
    emitShort(c, identifierConst(c, &s->as.decl.as.cls.id, s->line), s->line);

    Variable clsVar = declareVar(c, &s->as.decl.as.cls.id, s->as.decl.isStatic, s->line);
    defineVar(c, &clsVar, s->line);

    enterScope(c);

    JStarIdentifier superName = createIdentifier("super");
    Variable superVar = declareVar(c, &superName, false, s->line);
    defineVar(c, &superVar, s->line);

    if(!s->as.decl.as.cls.sup) {
        emitOpcode(c, OP_GET_OBJECT, s->line);
    } else {
        compileExpr(c, s->as.decl.as.cls.sup);
    }

    compileVarLit(c, &s->as.decl.as.cls.id, false, s->line);
    emitOpcode(c, OP_SUBCLASS, s->line);

    compileMethods(c, s);

    emitOpcode(c, OP_POP, s->line);
    exitScope(c);

    ext_vector(JStarExpr*) decorators = s->as.decl.decorators;

    if(ext_vec_size(decorators)) {
        compileDecorators(c, decorators);
        compileVarLit(c, &s->as.decl.as.cls.id, false, s->line);
        callDecorators(c, decorators);

        compileVarLit(c, &s->as.decl.as.cls.id, true, s->line);
        emitOpcode(c, OP_POP, s->line);
    }
}

static void compileFunDecl(Compiler* c, const JStarStmt* s) {
    const JStarIdentifier* funName = &s->as.decl.as.fun.id;
    Variable funVar = declareVar(c, funName, s->as.decl.isStatic, s->line);

    // If local initialize the variable in order to permit the function to reference itself
    if(funVar.scope == VAR_LOCAL) {
        initializeVar(c, &funVar);
    }

    ext_vector(JStarExpr*) decorators = s->as.decl.decorators;

    compileDecorators(c, decorators);
    compileFunction(c, TYPE_FUNC, copyString(c->vm, funName->name, funName->length), s);
    callDecorators(c, decorators);

    defineVar(c, &funVar, s->line);
}

static void compileNativeDecl(Compiler* c, const JStarStmt* s) {
    const JStarIdentifier* natName = &s->as.decl.as.native.id;
    Variable natVar = declareVar(c, natName, s->as.decl.isStatic, s->line);

    ext_vector(JStarExpr*) decorators = s->as.decl.decorators;

    compileDecorators(c, decorators);
    compileNative(c, copyString(c->vm, natName->name, natName->length), OP_NATIVE, s);
    callDecorators(c, decorators);

    defineVar(c, &natVar, s->line);
}

static void compileVarDecl(Compiler* c, const JStarStmt* s) {
    int varsCount = 0;
    Variable vars[MAX_LOCALS];

    ext_vec_foreach(const JStarIdentifier* varName, s->as.decl.as.var.ids) {
        Variable var = declareVar(c, varName, s->as.decl.isStatic, s->line);
        if(varsCount == MAX_LOCALS) break;
        vars[varsCount++] = var;
    }

    ext_vector(JStarExpr*) decorators = s->as.decl.decorators;
    if(s->as.decl.as.var.isUnpack && ext_vec_size(decorators)) {
        error(c, decorators[0]->line, "Unpacking declaration cannot be decorated");
    }

    compileDecorators(c, decorators);

    if(s->as.decl.as.var.init != NULL) {
        JStarExpr* init = s->as.decl.as.var.init;
        bool isUnpack = s->as.decl.as.var.isUnpack;

        // Optimize constant unpacks by omitting tuple allocation
        if(isUnpack && IS_CONST_UNPACK(init->type)) {
            JStarExpr* exprs = getExpressions(init);
            compileConstUnpack(c, exprs, varsCount, s->as.decl.as.var.ids);
        } else {
            compileRval(c, init, &s->as.decl.as.var.ids[0]);
            if(isUnpack) {
                emitOpcode(c, OP_UNPACK, s->line);
                emitByte(c, (uint8_t)varsCount, s->line);
                adjustStackUsage(c, varsCount - 1);
            }
        }
    } else {
        // Default initialize the variables to null
        for(int i = 0; i < varsCount; i++) {
            emitOpcode(c, OP_NULL, s->line);
        }
    }

    callDecorators(c, decorators);

    // define in reverse order in order to assign correct
    // values to variables in case of a const unpack
    for(int i = varsCount - 1; i >= 0; i--) {
        defineVar(c, &vars[i], s->line);
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
        compileStatements(c, s->as.blockStmt.stmts);
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
        emitOpcode(c, OP_POP, 0);
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
        JSR_UNREACHABLE();
    }
}

static void resolveFwdRefs(Compiler* c) {
    ext_vec_foreach(const FwdRef* fwdRef, *c->fwdRefs) {
        const JStarIdentifier* id = &fwdRef->id;
        Variable global = resolveGlobal(c, id);
        if(global.scope == VAR_ERR) {
            error(c, fwdRef->line, "Cannot resolve name `%.*s`", id->length, id->name);
        }
    }
}

// -----------------------------------------------------------------------------
// API
// -----------------------------------------------------------------------------

ObjFunction* compile(JStarVM* vm, const char* filename, ObjModule* module, const JStarStmt* ast) {
    PROFILE_FUNC()

    ext_vector(JStarIdentifier) globals = NULL;
    ext_vector(FwdRef) fwdRefs = NULL;

    Compiler c;
    initCompiler(&c, vm, NULL, module, filename, TYPE_FUNC, &globals, &fwdRefs, ast);
    ObjFunction* func = function(&c, module, copyString(vm, "<main>", 6), ast);
    resolveFwdRefs(&c);
    endCompiler(&c);

    ext_vec_free(fwdRefs);
    ext_vec_free(globals);

    return c.hadError ? NULL : func;
}

void reachCompilerRoots(JStarVM* vm, Compiler* c) {
    PROFILE_FUNC()

    while(c != NULL) {
        reachObject(vm, (Obj*)c->func);
        c = c->prev;
    }
}
