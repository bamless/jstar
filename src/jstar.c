
#include "jstar.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "buffer.h"
#include "compiler.h"
#include "disassemble.h"
#include "gc.h"
#include "import.h"
#include "object.h"
#include "object_types.h"
#include "parse/ast.h"
#include "parse/lex.h"
#include "parse/parser.h"
#include "profile.h"
#include "serialize.h"
#include "value.h"
#include "value_hashtable.h"
#include "vm.h"

// -----------------------------------------------------------------------------
// API - The bulk of the API (jstar.h) implementation
// JStarNewVM, jsrInitRuntime and JStarFreeVM functions are implemented in vm.c
// -----------------------------------------------------------------------------

static bool validateSlot(const JStarVM* vm, int slot) {
    if(slot < 0) {
        return vm->sp + slot >= vm->apiStack;
    } else {
        return vm->apiStack + slot < vm->sp;
    }
}

static bool validateStack(const JStarVM* vm) {
    return (size_t)(vm->sp - vm->stack) <= vm->stackSz;
}

static int apiStackIndex(const JStarVM* vm, int slot) {
    if(slot < 0) {
        JSR_ASSERT(vm->sp + slot >= vm->apiStack, "API stack underflow");
        return vm->sp + slot - vm->apiStack;
    }
    JSR_ASSERT(vm->apiStack + slot < vm->sp, "API stack overflow");
    return slot;
}

static Value apiStackSlot(const JStarVM* vm, int slot) {
    if(slot < 0) {
        JSR_ASSERT(vm->sp + slot >= vm->apiStack, "API stack underflow");
        return vm->sp[slot];
    }
    JSR_ASSERT(vm->apiStack + slot < vm->sp, "API stack overflow");
    return vm->apiStack[slot];
}

static ObjModule* getModuleOrRaise(JStarVM* vm, const char* module) {
    ObjModule* res = module ? getModule(vm, copyCString(vm, module)) : vm->module;
    if(!res) {
        if(module) {
            jsrRaise(vm, "ImportException", "Module '%s' not found.", module);
        } else {
            jsrRaise(vm, "ImportException",
                     "No current module loaded, pass an explicit module name.");
        }
        return NULL;
    }
    return res;
}

void jsrPrintErrorCB(JStarVM* vm, JStarResult err, const char* file, JStarLoc loc,
                     const char* error) {
    (void)vm, (void)err;
    if(loc.line > 0) {
        fprintf(stderr, "%s:%d:%d error\n", file, loc.line, loc.col);
    } else {
        fprintf(stderr, "%s error\n", file);
    }
    fprintf(stderr, "%s\n", error);
}

static void parseError(const char* file, JStarLoc loc, const char* error, void* udata) {
    JStarVM* vm = udata;
    vm->errorCallback(vm, JSR_SYNTAX_ERR, file, loc, error);
}

JStarConf jsrGetConf(void) {
    return (JStarConf){
        100,
        1024 * 1024 * 20,  // 20 MiB
        2,
        &jsrPrintErrorCB,
        NULL,
        NULL,
        NULL,
    };
}

void* jsrGetCustomData(const JStarVM* vm) {
    return vm->userData;
}

static bool eval(JStarVM* vm, const char* path, ObjFunction* fn) {
    PROFILE_BEGIN_SESSION("jstar-eval.json")
    bool res = true;

    push(vm, OBJ_VAL(fn));
    vm->sp[-1] = OBJ_VAL(newClosure(vm, fn));
    moduleSetPath(vm, fn->proto.module, path);

    if(!jsrCall(vm, 0)) {
        jsrGetStacktrace(vm, -1);
        vm->errorCallback(vm, JSR_RUNTIME_ERR, path, (JStarLoc){0}, jsrGetString(vm, -1));
        pop(vm);
        res = false;
    }

    pop(vm);

#ifdef JSTAR_DBG_CACHE_STATS
    printf(" * Cache hits: %lu\n", vm->cacheHits);
    printf(" * Cache misses: %lu\n", vm->cacheMisses);
    printf(" * Hit the cache %.2f%% of the time\n",
           (vm->cacheHits + vm->cacheMisses) == 0
               ? 0.0
               : (double)vm->cacheHits / (vm->cacheHits + vm->cacheMisses) * 100);
    vm->cacheHits = vm->cacheMisses = 0;
#endif

    PROFILE_END_SESSION()

    return res;
}

static JStarResult evalString(JStarVM* vm, const char* path, const char* module, const char* src,
                              size_t len) {
    PROFILE_BEGIN_SESSION("jstar-eval.json")

    JStarStmt* program = jsrParse(path, src, len, parseError, &vm->astArena, vm);
    if(program == NULL) {
        jsrASTArenaReset(&vm->astArena);
        return JSR_SYNTAX_ERR;
    }

    ObjString* name = copyCString(vm, module);
    ObjFunction* fn = compileModule(vm, path, name, program);
    jsrASTArenaReset(&vm->astArena);

    if(fn == NULL) return JSR_COMPILE_ERR;
    if(!eval(vm, path, fn)) return JSR_RUNTIME_ERR;

    PROFILE_END_SESSION()
    return JSR_SUCCESS;
}

JStarResult jsrEvalString(JStarVM* vm, const char* path, const char* src) {
    return jsrEvalModuleString(vm, path, JSR_MAIN_MODULE, src);
}

JStarResult jsrEvalModuleString(JStarVM* vm, const char* path, const char* module,
                                const char* src) {
    return evalString(vm, path, module, src, strlen(src));
}

JStarResult jsrEval(JStarVM* vm, const char* path, const void* code, size_t len) {
    return jsrEvalModule(vm, path, JSR_MAIN_MODULE, code, len);
}

JStarResult jsrEvalModule(JStarVM* vm, const char* path, const char* module, const void* code,
                          size_t len) {
    if(!isCompiledCode(code, len)) {
        return evalString(vm, path, module, code, len);
    }
    ObjFunction* fn;
    ObjString* name = copyCString(vm, module);
    JStarResult res = deserializeModule(vm, path, name, code, len, &fn);
    if(res != JSR_SUCCESS) return res;
    if(!eval(vm, path, fn)) return JSR_RUNTIME_ERR;
    return JSR_SUCCESS;
}

JStarResult jsrCompileCode(JStarVM* vm, const char* path, const char* src, size_t len,
                           JStarBuffer* out) {
    JStarStmt* program = jsrParse(path, src, len, parseError, &vm->astArena, vm);
    if(program == NULL) {
        jsrASTArenaReset(&vm->astArena);
        return JSR_SYNTAX_ERR;
    }

    // The function won't be executed, only compiled, so pass null module
    ObjFunction* fn = compile(vm, path, NULL, program);
    jsrASTArenaReset(&vm->astArena);

    if(fn == NULL) {
        return JSR_COMPILE_ERR;
    }

    *out = serialize(vm, fn);
    return JSR_SUCCESS;
}

JStarResult jsrDisassembleCode(JStarVM* vm, const char* path, const void* code, size_t len) {
    if(!isCompiledCode(code, len)) {
        return JSR_DESERIALIZE_ERR;
    }

    ObjFunction* fn;
    ObjString* dummy = copyCString(vm, "");  // Use dummy module since the code won't be executed
    JStarResult res = deserializeModule(vm, path, dummy, code, len, &fn);

    if(res == JSR_SUCCESS) {
        disassembleFunction(fn);
    }

    return res;
}

static void callError(JStarVM* vm, int evalDepth, uint8_t argc) {
    // Restore stack and push the exception on top of it
    Value exception = pop(vm);
    vm->sp -= argc + 1;
    push(vm, exception);

    // If needed, finish to unwind the stack
    if(vm->frameCount > evalDepth) {
        unwindStack(vm, evalDepth);
    }
}

static bool executeCall(JStarVM* vm, int evalDepth) {
    if(vm->frameCount > evalDepth) {
        return runEval(vm, evalDepth);
    }
    return true;
}

bool jsrCall(JStarVM* vm, uint8_t argc) {
    int evalDepth = vm->frameCount;

    if(!callValue(vm, peekn(vm, argc), argc)) {
        callError(vm, evalDepth, argc);
        return false;
    }

    if(!executeCall(vm, evalDepth)) {
        return false;
    }

    return true;
}

bool jsrCallMethod(JStarVM* vm, const char* name, uint8_t argc) {
    JStarSymbol sym = {0};
    return jsrCallMethodCached(vm, name, argc, &sym);
}

JStarSymbol* jsrNewSymbol(JStarVM* vm) {
    JStarSymbol* sym = GC_ALLOC(vm, sizeof(*sym));
    *sym = (JStarSymbol){0};

    sym->next = vm->symbols;
    vm->symbols = sym;

    return sym;
}

void jsrFreeSymbol(JStarVM* vm, JStarSymbol* sym) {
    if(vm->symbols == sym) {
        vm->symbols = sym->next;
    }

    if(sym->prev != NULL) {
        sym->prev->next = sym->next;
    }
    if(sym->next != NULL) {
        sym->next->prev = sym->prev;
    }

    GC_FREE(vm, JStarSymbol, sym);
}

bool jsrCallMethodCached(JStarVM* vm, const char* name, uint8_t argc, JStarSymbol* sym) {
    int evalDepth = vm->frameCount;

    if(!invokeValue(vm, copyCString(vm, name), argc, &sym->sym)) {
        callError(vm, evalDepth, argc);
        return false;
    }

    if(!executeCall(vm, evalDepth)) {
        return false;
    }

    return true;
}

void jsrEvalBreak(JStarVM* vm) {
    if(vm->frameCount) vm->evalBreak = 1;
}

// TODO: unify with getStacktrace
void jsrPrintStacktrace(JStarVM* vm, int slot) {
    PROFILE_FUNC()

    Value exc = vm->apiStack[apiStackIndex(vm, slot)];
    JSR_ASSERT(isInstance(vm, exc, vm->excClass), "Top of stack isn't an exception");
    push(vm, exc);

    // Can fail with a stack overflow (for example if there is a cycle in exception causes)
    if(!jsrCallMethod(vm, "printStacktrace", 0)) {
        jsrPrintStacktrace(vm, -1);
    }

    pop(vm);
}

// TODO: unify with printStacktrace
void jsrGetStacktrace(JStarVM* vm, int slot) {
    PROFILE_FUNC()

    Value exc = vm->apiStack[apiStackIndex(vm, slot)];
    JSR_ASSERT(isInstance(vm, exc, vm->excClass), "Top of stack isn't an exception");
    push(vm, exc);

    // Can fail with a stack overflow (for example if there is a cycle in exception causes)
    if(!jsrCallMethod(vm, "getStacktrace", 0)) {
        jsrGetStacktrace(vm, -1);
    }
}

void jsrRaiseException(JStarVM* vm, int slot) {
    PROFILE_FUNC()

    Value excVal = apiStackSlot(vm, slot);
    if(!isInstance(vm, excVal, vm->excClass)) {
        jsrRaise(vm, "TypeException", "Can only raise Exception instances");
        return;
    }

    ObjInstance* exception = (ObjInstance*)AS_OBJ(excVal);
    ObjClass* cls = exception->base.cls;

    Value value = NULL_VAL;
    instanceGetField(cls, exception, vm->excTrace, &value);
    ObjStackTrace* st = IS_STACK_TRACE(value) ? (ObjStackTrace*)AS_OBJ(value) : newStackTrace(vm);
    st->lastTracedFrame = -1;

    instanceSetField(vm, cls, exception, vm->excTrace, OBJ_VAL(st));

    // Place the exception on top of the stack if not already
    if(!valueEquals(excVal, vm->sp[-1])) {
        push(vm, excVal);
    }
}

void jsrRaise(JStarVM* vm, const char* cls, const char* err, ...) {
    PROFILE_FUNC()

    if(!jsrGetGlobal(vm, NULL, cls)) {
        return;
    }

    JSR_ASSERT(IS_CLASS(peek(vm)), "Trying to raise a non-class");
    ObjClass* excCls = AS_CLASS(peek(vm));

    if(!isSubClass(excCls, vm->excClass)) {
        jsrRaise(vm, "TypeException", "Can only raise Exception subclasses");
        return;
    }

    ObjInstance* exception = newInstance(vm, excCls);

    pop(vm);
    push(vm, OBJ_VAL(exception));

    ObjStackTrace* st = newStackTrace(vm);
    instanceSetField(vm, excCls, exception, vm->excTrace, OBJ_VAL(st));

    if(err != NULL) {
        JStarBuffer error;
        jsrBufferInit(vm, &error);

        va_list args;
        va_start(args, err);
        jsrBufferAppendvf(&error, err, args);
        va_end(args);

        ObjString* errorString = jsrBufferToString(&error);
        instanceSetField(vm, excCls, exception, vm->excErr, OBJ_VAL(errorString));
    }
}

void jsrInitCommandLineArgs(JStarVM* vm, int argc, const char** argv) {
    ObjList* argvList = vm->argv;
    argvList->count = 0;
    for(int i = 0; i < argc; i++) {
        Value arg = OBJ_VAL(copyCString(vm, argv[i]));
        listAppend(vm, argvList, arg);
    }
}

void jsrEnsureStack(JStarVM* vm, size_t needed) {
    reserveStack(vm, needed);
}

bool jsrValidateSlot(const JStarVM* vm, int slot) {
    return validateSlot(vm, slot);
}

bool jsrValidateStack(const JStarVM* vm) {
    return validateStack(vm);
}

bool jsrIs(const JStarVM* vm, int slot, int classSlot) {
    Value v = apiStackSlot(vm, slot);
    Value cls = apiStackSlot(vm, classSlot);
    return IS_CLASS(cls) ? isInstance(vm, v, AS_CLASS(cls)) : false;
}

bool jsrRawEquals(const JStarVM* vm, int slot1, int slot2) {
    Value v1 = apiStackSlot(vm, slot1);
    Value v2 = apiStackSlot(vm, slot2);
    return valueEquals(v1, v2);
}

bool jsrEquals(JStarVM* vm, int slot1, int slot2) {
    Value v1 = apiStackSlot(vm, slot1);
    Value v2 = apiStackSlot(vm, slot2);

    if(IS_NUM(v1) || IS_NULL(v1) || IS_BOOL(v1)) {
        return valueEquals(v1, v2);
    }

    Value eqOverload;
    ObjClass* cls = getClass(vm, v1);
    if(hashTableValueGet(&cls->methods, vm->specialMethods[METH_EQ], &eqOverload)) {
        push(vm, v1);
        push(vm, v2);
        if(jsrCallMethod(vm, "__eq__", 1)) {
            return valueToBool(pop(vm));
        } else {
            return pop(vm), false;
        }
    } else {
        return valueEquals(v1, v2);
    }
}

static void checkStack(const JStarVM* vm) {
    JSR_ASSERT(validateStack(vm), "API Stack overflow");
}

void jsrPushNumber(JStarVM* vm, double number) {
    checkStack(vm);
    push(vm, NUM_VAL(number));
}

void jsrPushBoolean(JStarVM* vm, bool boolean) {
    checkStack(vm);
    push(vm, BOOL_VAL(boolean));
}

void jsrPushStringSz(JStarVM* vm, const char* string, size_t length) {
    checkStack(vm);
    // TODO: Rework string interning
    ObjString* str = allocateString(vm, length);
    memcpy(str->data, string, length);
    push(vm, OBJ_VAL(str));
}

void jsrPushString(JStarVM* vm, const char* string) {
    jsrPushStringSz(vm, string, strlen(string));
}

void jsrPushHandle(JStarVM* vm, void* handle) {
    checkStack(vm);
    push(vm, HANDLE_VAL(handle));
}

void jsrPushNull(JStarVM* vm) {
    checkStack(vm);
    push(vm, NULL_VAL);
}

void jsrPushList(JStarVM* vm) {
    checkStack(vm);
    push(vm, OBJ_VAL(newList(vm, 16)));
}

void jsrPushTuple(JStarVM* vm, size_t size) {
    checkStack(vm);
    ObjTuple* tup = newTuple(vm, size);
    for(size_t i = 1; i <= size; i++) {
        tup->items[size - i] = pop(vm);
    }
    push(vm, OBJ_VAL(tup));
}

void jsrPushTable(JStarVM* vm) {
    checkStack(vm);
    push(vm, OBJ_VAL(newTable(vm)));
}

void jsrPushValue(JStarVM* vm, int slot) {
    checkStack(vm);
    push(vm, apiStackSlot(vm, slot));
}

void* jsrPushUserdata(JStarVM* vm, size_t size, void (*finalize)(void*)) {
    checkStack(vm);
    ObjUserdata* udata = newUserData(vm, size, finalize);
    push(vm, OBJ_VAL(udata));
    return (void*)udata->data;
}

bool jsrPushNative(JStarVM* vm, const char* module, const char* name, JStarNative nat,
                   uint8_t argc) {
    checkStack(vm);
    ObjModule* mod = getModuleOrRaise(vm, module);
    if(!mod) return false;

    ObjString* nativeName = copyCString(vm, name);
    ObjNative* native = newNative(vm, mod, nativeName, argc, 0, false, nat);

    push(vm, OBJ_VAL(native));
    return true;
}

void jsrPop(JStarVM* vm) {
    JSR_ASSERT(validateSlot(vm, -1), "Popping past frame boundary");
    pop(vm);
}

void jsrPopN(JStarVM* vm, int n) {
    JSR_ASSERT(validateSlot(vm, -n), "Popping past frame boundary");
    for(int i = 0; i < n; i++) {
        jsrPop(vm);
    }
}

int jsrTop(const JStarVM* vm) {
    return apiStackIndex(vm, -1);
}

bool jsrIter(JStarVM* vm, int iterable, int res, bool* err) {
    jsrEnsureStack(vm, 2);
    jsrPushValue(vm, iterable);
    jsrPushValue(vm, res < 0 ? res - 1 : res);

    if(!jsrCallMethod(vm, "__iter__", 1)) {
        return (*err = true);
    }
    if(jsrIsNull(vm, -1) || (jsrIsBoolean(vm, -1) && !jsrGetBoolean(vm, -1))) {
        pop(vm);
        return false;
    }

    Value resVal = pop(vm);
    vm->apiStack[apiStackIndex(vm, res)] = resVal;

    *err = false;
    return true;
}

bool jsrNext(JStarVM* vm, int iterable, int res) {
    jsrPushValue(vm, iterable);
    jsrPushValue(vm, res < 0 ? res - 1 : res);
    if(!jsrCallMethod(vm, "__next__", 1)) return false;
    return true;
}

void jsrListAppend(JStarVM* vm, int slot) {
    Value lst = apiStackSlot(vm, slot);
    JSR_ASSERT(IS_LIST(lst), "Not a list");
    listAppend(vm, AS_LIST(lst), peek(vm));
}

void jsrListInsert(JStarVM* vm, size_t i, int slot) {
    Value lstVal = apiStackSlot(vm, slot);
    JSR_ASSERT(IS_LIST(lstVal), "Not a list");
    ObjList* lst = AS_LIST(lstVal);
    JSR_ASSERT(i < lst->count, "Out of bounds");
    listInsert(vm, lst, (size_t)i, peek(vm));
}

void jsrListRemove(JStarVM* vm, size_t i, int slot) {
    Value lstVal = apiStackSlot(vm, slot);
    JSR_ASSERT(IS_LIST(lstVal), "Not a list");
    ObjList* lst = AS_LIST(lstVal);
    JSR_ASSERT(i < lst->count, "Out of bounds");
    listRemove(lst, (size_t)i);
}

void jsrListGet(JStarVM* vm, size_t i, int slot) {
    Value lstVal = apiStackSlot(vm, slot);
    JSR_ASSERT(IS_LIST(lstVal), "Not a list");
    ObjList* lst = AS_LIST(lstVal);
    JSR_ASSERT(i < lst->count, "Out of bounds");
    push(vm, lst->items[i]);
}

size_t jsrListGetLength(const JStarVM* vm, int slot) {
    Value lst = apiStackSlot(vm, slot);
    JSR_ASSERT(IS_LIST(lst), "Not a list");
    return AS_LIST(lst)->count;
}

void jsrTupleGet(JStarVM* vm, size_t i, int slot) {
    Value tupVal = apiStackSlot(vm, slot);
    JSR_ASSERT(IS_TUPLE(tupVal), "Not a tuple");
    ObjTuple* tuple = AS_TUPLE(tupVal);
    JSR_ASSERT(i < tuple->count, "Out of bounds");
    push(vm, tuple->items[i]);
}

size_t jsrTupleGetLength(const JStarVM* vm, int slot) {
    Value tup = apiStackSlot(vm, slot);
    JSR_ASSERT(IS_TUPLE(tup), "Not a tuple");
    return AS_TUPLE(tup)->count;
}

bool jsrSubscriptGet(JStarVM* vm, int slot) {
    push(vm, apiStackSlot(vm, slot));
    swapStackSlots(vm, -1, -2);
    return getValueSubscript(vm);
}

bool jsrSubscriptSet(JStarVM* vm, int slot) {
    swapStackSlots(vm, -1, -2);
    push(vm, apiStackSlot(vm, slot));
    return setValueSubscript(vm);
}

size_t jsrGetLength(JStarVM* vm, int slot) {
    push(vm, apiStackSlot(vm, slot));

    if(!jsrCallMethod(vm, "__len__", 0)) {
        return SIZE_MAX;
    }

    size_t size = jsrGetNumber(vm, -1);
    pop(vm);

    return size;
}

bool jsrSetField(JStarVM* vm, int slot, const char* name) {
    JStarSymbol sym = {0};
    return jsrSetFieldCached(vm, slot, name, &sym);
}

bool jsrSetFieldCached(JStarVM* vm, int slot, const char* name, JStarSymbol* sym) {
    push(vm, apiStackSlot(vm, slot));
    return setValueField(vm, copyCString(vm, name), &sym->sym);
}

bool jsrGetField(JStarVM* vm, int slot, const char* name) {
    JStarSymbol sym = {0};
    return jsrGetFieldCached(vm, slot, name, &sym);
}

bool jsrGetFieldCached(JStarVM* vm, int slot, const char* name, JStarSymbol* sym) {
    push(vm, apiStackSlot(vm, slot));
    return getValueField(vm, copyCString(vm, name), &sym->sym);
}

bool jsrSetGlobal(JStarVM* vm, const char* module, const char* name) {
    JStarSymbol sym = {0};
    return jsrSetGlobalCached(vm, module, name, &sym);
}

bool jsrSetGlobalCached(JStarVM* vm, const char* module, const char* name, JStarSymbol* sym) {
    ObjModule* mod = getModuleOrRaise(vm, module);
    if(!mod) {
        return false;
    }
    setGlobalName(vm, mod, copyCString(vm, name), &sym->sym);
    return true;
}

bool jsrGetGlobal(JStarVM* vm, const char* module, const char* name) {
    JStarSymbol sym = {0};
    return jsrGetGlobalCached(vm, module, name, &sym);
}

bool jsrGetGlobalCached(JStarVM* vm, const char* module, const char* name, JStarSymbol* sym) {
    ObjModule* mod = getModuleOrRaise(vm, module);
    if(!mod) {
        return false;
    }
    return getGlobalName(vm, mod, copyCString(vm, name), &sym->sym);
}

void jsrBindNative(JStarVM* vm, int clsSlot, int natSlot) {
    Value cls = apiStackSlot(vm, clsSlot);
    Value nat = apiStackSlot(vm, natSlot);
    JSR_ASSERT(IS_CLASS(cls), "clsSlot is not a Class");
    JSR_ASSERT(IS_NATIVE(nat), "natSlot is not a Native Function");
    hashTableValuePut(&AS_CLASS(cls)->methods, AS_NATIVE(nat)->proto.name, nat);
}

void* jsrGetUserdata(JStarVM* vm, int slot) {
    JSR_ASSERT(IS_USERDATA(apiStackSlot(vm, slot)), "slot is not a Userdatum");
    return (void*)AS_USERDATA(apiStackSlot(vm, slot))->data;
}

double jsrGetNumber(const JStarVM* vm, int slot) {
    JSR_ASSERT(IS_NUM(apiStackSlot(vm, slot)), "slot is not a Number");
    return AS_NUM(apiStackSlot(vm, slot));
}

const char* jsrGetString(const JStarVM* vm, int slot) {
    JSR_ASSERT(IS_STRING(apiStackSlot(vm, slot)), "slot is not a String");
    return AS_STRING(apiStackSlot(vm, slot))->data;
}

size_t jsrGetStringSz(const JStarVM* vm, int slot) {
    JSR_ASSERT(IS_STRING(apiStackSlot(vm, slot)), "slot is not a String");
    return AS_STRING(apiStackSlot(vm, slot))->length;
}

bool jsrGetBoolean(const JStarVM* vm, int slot) {
    JSR_ASSERT(IS_BOOL(apiStackSlot(vm, slot)), "slot is not a Boolean");
    return AS_BOOL(apiStackSlot(vm, slot));
}

void* jsrGetHandle(const JStarVM* vm, int slot) {
    JSR_ASSERT(IS_HANDLE(apiStackSlot(vm, slot)), "slot is not an Handle");
    return AS_HANDLE(apiStackSlot(vm, slot));
}

bool jsrIsNumber(const JStarVM* vm, int slot) {
    return IS_NUM(apiStackSlot(vm, slot));
}

bool jsrIsInteger(const JStarVM* vm, int slot) {
    return IS_INT(apiStackSlot(vm, slot));
}

bool jsrIsString(const JStarVM* vm, int slot) {
    return IS_STRING(apiStackSlot(vm, slot));
}

bool jsrIsList(const JStarVM* vm, int slot) {
    return IS_LIST(apiStackSlot(vm, slot));
}

bool jsrIsTuple(const JStarVM* vm, int slot) {
    return IS_TUPLE(apiStackSlot(vm, slot));
}

bool jsrIsBoolean(const JStarVM* vm, int slot) {
    return IS_BOOL(apiStackSlot(vm, slot));
}

bool jsrIsNull(const JStarVM* vm, int slot) {
    return IS_NULL(apiStackSlot(vm, slot));
}

bool jsrIsInstance(const JStarVM* vm, int slot) {
    return IS_INSTANCE(apiStackSlot(vm, slot));
}

bool jsrIsHandle(const JStarVM* vm, int slot) {
    return IS_HANDLE(apiStackSlot(vm, slot));
}

bool jsrIsTable(const JStarVM* vm, int slot) {
    return IS_TABLE(apiStackSlot(vm, slot));
}

bool jsrIsFunction(const JStarVM* vm, int slot) {
    Value val = apiStackSlot(vm, slot);
    return IS_CLOSURE(val) || IS_NATIVE(val) || IS_BOUND_METHOD(val);
}

bool jsrIsUserdata(const JStarVM* vm, int slot) {
    Value val = apiStackSlot(vm, slot);
    return IS_USERDATA(val);
}

bool jsrCheckNumber(JStarVM* vm, int slot, const char* name) {
    if(!jsrIsNumber(vm, slot)) JSR_RAISE(vm, "TypeException", "%s must be a number.", name);
    return true;
}

bool jsrCheckInt(JStarVM* vm, int slot, const char* name) {
    if(!jsrIsInteger(vm, slot)) JSR_RAISE(vm, "TypeException", "%s must be an integer.", name);
    return true;
}

bool jsrCheckString(JStarVM* vm, int slot, const char* name) {
    if(!jsrIsString(vm, slot)) JSR_RAISE(vm, "TypeException", "%s must be a String.", name);
    return true;
}

bool jsrCheckList(JStarVM* vm, int slot, const char* name) {
    if(!jsrIsList(vm, slot)) JSR_RAISE(vm, "TypeException", "%s must be a List.", name);
    return true;
}

bool jsrCheckTuple(JStarVM* vm, int slot, const char* name) {
    if(!jsrIsTuple(vm, slot)) JSR_RAISE(vm, "TypeException", "%s must be a Tuple.", name);
    return true;
}

bool jsrCheckBoolean(JStarVM* vm, int slot, const char* name) {
    if(!jsrIsBoolean(vm, slot)) JSR_RAISE(vm, "TypeException", "%s must be a Boolean.", name);
    return true;
}

bool jsrCheckNull(JStarVM* vm, int slot, const char* name) {
    if(!jsrIsNull(vm, slot)) JSR_RAISE(vm, "TypeException", "%s must be null.", name);
    return true;
}

bool jsrCheckInstance(JStarVM* vm, int slot, const char* name) {
    if(!jsrIsInstance(vm, slot)) JSR_RAISE(vm, "TypeException", "%s must be an instance.", name);
    return true;
}

bool jsrCheckHandle(JStarVM* vm, int slot, const char* name) {
    if(!jsrIsHandle(vm, slot)) JSR_RAISE(vm, "TypeException", "%s must be an Handle.", name);
    return true;
}

bool jsrCheckTable(JStarVM* vm, int slot, const char* name) {
    if(!jsrIsTable(vm, slot)) JSR_RAISE(vm, "TypeException", "%s must be a Table.", name);
    return true;
}

bool jsrCheckFunction(JStarVM* vm, int slot, const char* name) {
    if(!jsrIsFunction(vm, slot)) JSR_RAISE(vm, "TypeException", "%s must be a Function.", name);
    return true;
}

bool jsrCheckUserdata(JStarVM* vm, int slot, const char* name) {
    if(!jsrIsUserdata(vm, slot)) JSR_RAISE(vm, "TypeException", "%s must be a Userdata.", name);
    return true;
}

size_t jsrCheckIndexNum(JStarVM* vm, double i, size_t max) {
    if(i >= 0 && i < max) return (size_t)i;
    jsrRaise(vm, "IndexOutOfBoundException", "%g.", i);
    return SIZE_MAX;
}

size_t jsrCheckIndex(JStarVM* vm, int slot, size_t max, const char* name) {
    if(!jsrCheckInt(vm, slot, name)) return SIZE_MAX;
    double i = jsrGetNumber(vm, slot);
    return jsrCheckIndexNum(vm, i, max);
}
