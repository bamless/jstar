#include "jstar.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "buffer.h"
#include "code.h"
#include "compiler.h"
#include "disassemble.h"
#include "hashtable.h"
#include "import.h"
#include "lib/core/excs.h"
#include "object.h"
#include "parse/ast.h"
#include "parse/parser.h"
#include "profiler.h"
#include "serialize.h"
#include "value.h"
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
    ObjModule* mod = module ? getModule(vm, copyString(vm, module, strlen(module))) : vm->module;
    if(!mod) {
        if(module)
            jsrRaise(vm, "ImportException", "Module '%s' not found.", module);
        else
            jsrRaise(vm, "ImportException",
                     "No current module loaded, pass an explicit module name.");
        return NULL;
    }
    return mod;
}

void jsrPrintErrorCB(JStarVM* vm, JStarResult err, const char* file, int line, const char* error) {
    if(line >= 0) {
        fprintf(stderr, "File %s [line:%d]:\n", file, line);
    } else {
        fprintf(stderr, "File %s:\n", file);
    }
    fprintf(stderr, "%s\n", error);
}

static void parseError(const char* file, int line, const char* error, void* udata) {
    JStarVM* vm = udata;
    vm->errorCallback(vm, JSR_SYNTAX_ERR, file, line, error);
}

JStarConf jsrGetConf(void) {
    // Default configuration
    JStarConf conf;
    conf.startingStackSize = 100;
    conf.firstGCCollectionPoint = 1024 * 1024 * 20;  // 20 MiB
    conf.heapGrowRate = 2;
    conf.errorCallback = &jsrPrintErrorCB;
    conf.importCallback = NULL;
    conf.customData = NULL;
    return conf;
}

void* jsrGetCustomData(const JStarVM* vm) {
    return vm->customData;
}

static JStarResult evalStringLen(JStarVM* vm, const char* path, const char* module, const char* src,
                                 size_t len) {
    JStarStmt* program = jsrParse(path, src, len, parseError, vm);
    if(program == NULL) {
        return JSR_SYNTAX_ERR;
    }

    ObjString* name = copyString(vm, module, strlen(module));
    ObjFunction* fn = compileModule(vm, path, name, program);
    jsrStmtFree(program);

    if(fn == NULL) {
        return JSR_COMPILE_ERR;
    }

    push(vm, OBJ_VAL(fn));
    vm->sp[-1] = OBJ_VAL(newClosure(vm, fn));

    JStarResult res = jsrCall(vm, 0);
    if(res != JSR_SUCCESS) {
        jsrGetStacktrace(vm, -1);
        vm->errorCallback(vm, JSR_RUNTIME_ERR, path, -1, jsrGetString(vm, -1));
        pop(vm);
    }

    pop(vm);
    return res;
}

JStarResult jsrEvalString(JStarVM* vm, const char* path, const char* src) {
    PROFILE_FUNC()
    return jsrEvalModuleString(vm, path, JSR_MAIN_MODULE, src);
}

JStarResult jsrEvalModuleString(JStarVM* vm, const char* path, const char* module,
                                const char* src) {
    return evalStringLen(vm, path, module, src, strlen(src));
}

JStarResult jsrEval(JStarVM* vm, const char* path, const void* code, size_t len) {
    PROFILE_FUNC()
    return jsrEvalModule(vm, path, JSR_MAIN_MODULE, code, len);
}

JStarResult jsrEvalModule(JStarVM* vm, const char* path, const char* module, const void* code,
                          size_t len) {
    PROFILE_FUNC()

    if(!isCompiledCode(code, len)) {
        return evalStringLen(vm, path, module, code, len);
    }

    ObjFunction* fn;
    ObjString* name = copyString(vm, module, strlen(module));
    JStarResult res = deserializeModule(vm, path, name, code, len, &fn);

    if(res != JSR_SUCCESS) {
        return res;
    }

    push(vm, OBJ_VAL(fn));
    vm->sp[-1] = OBJ_VAL(newClosure(vm, fn));

    res = jsrCall(vm, 0);
    if(res != JSR_SUCCESS) {
        jsrGetStacktrace(vm, -1);
        vm->errorCallback(vm, JSR_RUNTIME_ERR, path, -1, jsrGetString(vm, -1));
        pop(vm);
    }

    pop(vm);
    return res;
}

JStarResult jsrCompileCode(JStarVM* vm, const char* path, const char* src, JStarBuffer* out) {
    PROFILE_FUNC()

    JStarStmt* program = jsrParse(path, src, strlen(src), parseError, vm);
    if(program == NULL) {
        return JSR_SYNTAX_ERR;
    }

    // The function won't be executed, only compiled, so pass null module
    ObjFunction* fn = compile(vm, path, NULL, program);
    jsrStmtFree(program);

    if(fn == NULL) {
        return JSR_COMPILE_ERR;
    }

    *out = serialize(vm, fn);
    return JSR_SUCCESS;
}

JStarResult jsrDisassembleCode(JStarVM* vm, const char* path, const void* code, size_t len) {
    PROFILE_FUNC()

    if(!isCompiledCode(code, len)) {
        return JSR_DESERIALIZE_ERR;
    }

    ObjFunction* fn;
    ObjString* dummy = copyString(vm, "", 0);  // Use dummy module since the code won't be executed
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

JStarResult jsrCall(JStarVM* vm, uint8_t argc) {
    int evalDepth = vm->frameCount;

    if(!callValue(vm, peekn(vm, argc), argc)) {
        callError(vm, evalDepth, argc);
        return JSR_RUNTIME_ERR;
    }

    if(!executeCall(vm, evalDepth)) {
        return JSR_RUNTIME_ERR;
    }

    return JSR_SUCCESS;
}

JStarResult jsrCallMethod(JStarVM* vm, const char* name, uint8_t argc) {
    int evalDepth = vm->frameCount;

    // TODO: expose a 'Symbol' version of this method to let the user cache the symbol
    Symbol sym = (Symbol){.key = NULL};
    if(!invokeValue(vm, copyString(vm, name, strlen(name)), argc, &sym)) {
        callError(vm, evalDepth, argc);
        return JSR_RUNTIME_ERR;
    }

    if(!executeCall(vm, evalDepth)) {
        return JSR_RUNTIME_ERR;
    }

    return JSR_SUCCESS;
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
    if(jsrCallMethod(vm, "printStacktrace", 0) != JSR_SUCCESS) {
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
    if(jsrCallMethod(vm, "getStacktrace", 0) != JSR_SUCCESS) {
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

    ObjString* stField = copyString(vm, EXC_TRACE, strlen(EXC_TRACE));
    push(vm, OBJ_VAL(stField));

    Value value = NULL_VAL;
    getField(vm, cls, exception, stField, &value);
    ObjStackTrace* st = IS_STACK_TRACE(value) ? (ObjStackTrace*)AS_OBJ(value) : newStackTrace(vm);
    st->lastTracedFrame = -1;

    setField(vm, cls, exception, stField, OBJ_VAL(st));
    pop(vm);

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

    ObjInstance* exception = newInstance(vm, AS_CLASS(pop(vm)));
    ObjClass* excCls = exception->base.cls;
    if(!isInstance(vm, OBJ_VAL(exception), vm->excClass)) {
        jsrRaise(vm, "TypeException", "Can only raise Exception instances.");
    }

    push(vm, OBJ_VAL(exception));

    ObjStackTrace* st = newStackTrace(vm);
    setField(vm, excCls, exception, copyString(vm, EXC_TRACE, strlen(EXC_TRACE)), OBJ_VAL(st));

    if(err != NULL) {
        JStarBuffer error;
        jsrBufferInit(vm, &error);

        va_list args;
        va_start(args, err);
        jsrBufferAppendvf(&error, err, args);
        va_end(args);

        ObjString* errorField = copyString(vm, EXC_ERR, strlen(EXC_ERR));
        ObjString* errorString = jsrBufferToString(&error);
        setField(vm, excCls, exception, errorField, OBJ_VAL(errorString));
    }
}

void jsrInitCommandLineArgs(JStarVM* vm, int argc, const char** argv) {
    ObjList* argvList = vm->argv;
    argvList->size = 0;
    for(int i = 0; i < argc; i++) {
        Value arg = OBJ_VAL(copyString(vm, argv[i], strlen(argv[i])));
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

bool jsrReadFile(JStarVM* vm, const char* path, JStarBuffer* out) {
    bool res = false;
    int saveErrno;

    FILE* src = fopen(path, "rb");
    if(src == NULL) {
        return false;
    }

    if(fseek(src, 0, SEEK_END)) {
        goto exit_file;
    }

    long size = ftell(src);
    if(size < 0) goto exit_file;
    rewind(src);

    jsrBufferInitCapacity(vm, out, size + 1);

    size_t read = fread(out->data, 1, size, src);
    if(read < (size_t)size) {
        goto exit_buf;
    }

    out->size = size;

    if(!isCompiledCode(out->data, size)) {
        out->data[size] = '\0';
    }

    res = true;
    goto exit_file;

exit_buf:;
    saveErrno = errno;
    jsrBufferFree(out);
    errno = saveErrno;

exit_file:
    saveErrno = errno;
    if(fclose(src)) return false;
    errno = saveErrno;

    return res;
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
    if(hashTableGet(&cls->methods, vm->methodSyms[SYM_EQ], &eqOverload)) {
        push(vm, v1);
        push(vm, v2);
        JStarResult res = jsrCallMethod(vm, "__eq__", 1);
        if(res == JSR_SUCCESS)
            return valueToBool(pop(vm));
        else
            return pop(vm), false;
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
        tup->arr[size - i] = pop(vm);
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

    ObjString* nativeName = copyString(vm, name, strlen(name));
    push(vm, OBJ_VAL(nativeName));
    ObjNative* native = newNative(vm, mod, argc, 0, false);
    native->proto.name = nativeName;
    native->fn = nat;
    pop(vm);

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

bool jsrSetGlobal(JStarVM* vm, const char* module, const char* name) {
    ObjModule* mod = getModuleOrRaise(vm, module);
    if(!mod) return false;
    hashTablePut(&mod->globals, copyString(vm, name, strlen(name)), peek(vm));
    return true;
}

bool jsrIter(JStarVM* vm, int iterable, int res, bool* err) {
    jsrEnsureStack(vm, 2);
    jsrPushValue(vm, iterable);
    jsrPushValue(vm, res < 0 ? res - 1 : res);

    if(jsrCallMethod(vm, "__iter__", 1) != JSR_SUCCESS) {
        return *err = true;
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
    if(jsrCallMethod(vm, "__next__", 1) != JSR_SUCCESS) return false;
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
    JSR_ASSERT(i < lst->size, "Out of bounds");
    listInsert(vm, lst, (size_t)i, peek(vm));
}

void jsrListRemove(JStarVM* vm, size_t i, int slot) {
    Value lstVal = apiStackSlot(vm, slot);
    JSR_ASSERT(IS_LIST(lstVal), "Not a list");
    ObjList* lst = AS_LIST(lstVal);
    JSR_ASSERT(i < lst->size, "Out of bounds");
    listRemove(vm, lst, (size_t)i);
}

void jsrListGet(JStarVM* vm, size_t i, int slot) {
    Value lstVal = apiStackSlot(vm, slot);
    JSR_ASSERT(IS_LIST(lstVal), "Not a list");
    ObjList* lst = AS_LIST(lstVal);
    JSR_ASSERT(i < lst->size, "Out of bounds");
    push(vm, lst->arr[i]);
}

size_t jsrListGetLength(const JStarVM* vm, int slot) {
    Value lst = apiStackSlot(vm, slot);
    JSR_ASSERT(IS_LIST(lst), "Not a list");
    return AS_LIST(lst)->size;
}

void jsrTupleGet(JStarVM* vm, size_t i, int slot) {
    Value tupVal = apiStackSlot(vm, slot);
    JSR_ASSERT(IS_TUPLE(tupVal), "Not a tuple");
    ObjTuple* tuple = AS_TUPLE(tupVal);
    JSR_ASSERT(i < tuple->size, "Out of bounds");
    push(vm, tuple->arr[i]);
}

size_t jsrTupleGetLength(const JStarVM* vm, int slot) {
    Value tup = apiStackSlot(vm, slot);
    JSR_ASSERT(IS_TUPLE(tup), "Not a tuple");
    return AS_TUPLE(tup)->size;
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

    if(jsrCallMethod(vm, "__len__", 0) != JSR_SUCCESS) {
        return SIZE_MAX;
    }

    size_t size = jsrGetNumber(vm, -1);
    pop(vm);

    return size;
}

bool jsrSetField(JStarVM* vm, int slot, const char* name) {
    push(vm, apiStackSlot(vm, slot));
    // TODO: expose a 'Symbol' version of this method to let the user cache the symbol
    Symbol sym = (Symbol){.key = NULL};
    return setValueField(vm, copyString(vm, name, strlen(name)), &sym);
}

bool jsrGetField(JStarVM* vm, int slot, const char* name) {
    push(vm, apiStackSlot(vm, slot));
    // TODO: expose a 'Symbol' version of this method to let the user cache the symbol
    Symbol sym = (Symbol){.key = NULL};
    return getValueField(vm, copyString(vm, name, strlen(name)), &sym);
}

bool jsrGetGlobal(JStarVM* vm, const char* module, const char* name) {
    ObjModule* mod = getModuleOrRaise(vm, module);
    if(!mod) return false;

    Value res;
    ObjString* nameStr = copyString(vm, name, strlen(name));
    if(!hashTableGet(&mod->globals, nameStr, &res)) {
        jsrRaise(vm, "NameException", "Name %s not definied in module %s.", name, module);
        return false;
    }

    push(vm, res);
    return true;
}

void jsrBindNative(JStarVM* vm, int clsSlot, int natSlot) {
    Value cls = apiStackSlot(vm, clsSlot);
    Value nat = apiStackSlot(vm, natSlot);
    JSR_ASSERT(IS_CLASS(cls), "clsSlot is not a Class");
    JSR_ASSERT(IS_NATIVE(nat), "natSlot is not a Native Function");
    hashTablePut(&AS_CLASS(cls)->methods, AS_NATIVE(nat)->proto.name, nat);
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
