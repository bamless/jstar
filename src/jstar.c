#include "jstar.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "compiler.h"
#include "const.h"
#include "disassemble.h"
#include "hashtable.h"
#include "import.h"
#include "object.h"
#include "parse/ast.h"
#include "parse/parser.h"
#include "profiler.h"
#include "serialize.h"
#include "util.h"
#include "value.h"
#include "vm.h"

// -----------------------------------------------------------------------------
// API - The bulk of the API (jstar.h) implementation
// JStarNewVM and JStarFreeVM functions are implemented in vm.c
// -----------------------------------------------------------------------------

static int apiStackIndex(JStarVM* vm, int slot) {
    ASSERT(vm->sp - slot > vm->apiStack, "API stack slot would be negative");
    ASSERT(vm->apiStack + slot < vm->sp, "API stack overflow");
    if(slot < 0) return vm->sp + slot - vm->apiStack;
    return slot;
}

static Value apiStackSlot(JStarVM* vm, int slot) {
    ASSERT(vm->sp - slot > vm->apiStack, "API stack slot would be negative");
    ASSERT(vm->apiStack + slot < vm->sp, "API stack overflow");
    if(slot < 0) return vm->sp[slot];
    return vm->apiStack[slot];
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
    JStarConf conf;
    conf.stackSize = STACK_SZ;
    conf.initGC = INIT_GC;
    conf.heapGrowRate = HEAP_GROW_RATE;
    conf.errorCallback = &jsrPrintErrorCB;
    conf.customData = NULL;
    return conf;
}

void* jsrGetCustomData(JStarVM* vm) {
    return vm->customData;
}

JStarResult jsrEvalString(JStarVM* vm, const char* path, const char* src) {
    PROFILE_FUNC()
    return jsrEvalModuleString(vm, path, JSR_MAIN_MODULE, src);
}

JStarResult jsrEvalModuleString(JStarVM* vm, const char* path, const char* module,
                                const char* src) {
    PROFILE_FUNC()

    JStarStmt* program = jsrParse(path, src, parseError, vm);
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

JStarResult jsrEval(JStarVM* vm, const char* path, const JStarBuffer* code) {
    PROFILE_FUNC()
    return jsrEvalModule(vm, path, JSR_MAIN_MODULE, code);
}

JStarResult jsrEvalModule(JStarVM* vm, const char* path, const char* module,
                          const JStarBuffer* code) {
    PROFILE_FUNC()

    if(!isCompiledCode(code)) {
        return jsrEvalModuleString(vm, path, module, code->data);
    }

    JStarResult err;
    ObjString* name = copyString(vm, module, strlen(module));
    ObjFunction* fn = deserializeModule(vm, path, name, code, &err);

    if(fn == NULL) {
        return err;
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

JStarResult jsrCompileCode(JStarVM* vm, const char* path, const char* src, JStarBuffer* out) {
    PROFILE_FUNC()

    JStarStmt* program = jsrParse(path, src, parseError, vm);
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

JStarResult jsrDisassembleCode(JStarVM* vm, const char* path, const JStarBuffer* code) {
    PROFILE_FUNC()

    if(!isCompiledCode(code)) {
        return JSR_DESERIALIZE_ERR;
    }

    JStarResult ret;
    ObjString* dummy = copyString(vm, "", 0);  // Use dummy module since the code won't be executed
    ObjFunction* fn = deserializeModule(vm, path, dummy, code, &ret);

    if(ret == JSR_SUCCESS) {
        disassembleFunction(fn);
    }

    return ret;
}

static bool executeCall(JStarVM* vm, int evalDepth, size_t base) {
    // Nothing to do, must have been a native call
    if(vm->frameCount <= evalDepth) return true;

    // Eval the function
    if(!runEval(vm, evalDepth)) {
        // Exception was thrown, push it as a result
        Value exception = pop(vm);
        vm->sp = vm->stack + base;
        push(vm, exception);
        return false;
    }

    return true;
}

static void finishUnwind(JStarVM* vm, int evalDepth, size_t base) {
    // If needed, finish to unwind the stack
    if(vm->frameCount > evalDepth) {
        unwindStack(vm, evalDepth);
    }

    // Push the exception as a result
    Value exception = pop(vm);
    vm->sp = vm->stack + base;
    push(vm, exception);
}

JStarResult jsrCall(JStarVM* vm, uint8_t argc) {
    int evalDepth = vm->frameCount;
    size_t base = vm->sp - vm->stack - argc - 1;

    if(vm->reentrantCalls + 1 == MAX_REENTRANT) {
        vm->sp = vm->stack + base;
        jsrRaise(vm, "StackOverflowException", "Exceeded maximum number of reentrant calls");
        return JSR_RUNTIME_ERR;
    }

    vm->reentrantCalls++;

    if(!callValue(vm, peekn(vm, argc), argc)) {
        vm->reentrantCalls--;
        finishUnwind(vm, evalDepth, base);
        return JSR_RUNTIME_ERR;
    }

    if(!executeCall(vm, evalDepth, base)) {
        vm->reentrantCalls--;
        return JSR_RUNTIME_ERR;
    }

    vm->reentrantCalls--;
    return JSR_SUCCESS;
}

JStarResult jsrCallMethod(JStarVM* vm, const char* name, uint8_t argc) {
    int evalDepth = vm->frameCount;
    size_t base = vm->sp - vm->stack - argc - 1;

    if(vm->reentrantCalls + 1 == MAX_REENTRANT) {
        vm->sp = vm->stack + base;
        jsrRaise(vm, "StackOverflowException", "Exceeded maximum number of reentrant calls");
        return JSR_RUNTIME_ERR;
    }

    vm->reentrantCalls++;

    if(!invokeValue(vm, copyString(vm, name, strlen(name)), argc)) {
        vm->reentrantCalls--;
        finishUnwind(vm, evalDepth, base);
        return JSR_RUNTIME_ERR;
    }

    if(!executeCall(vm, evalDepth, base)) {
        vm->reentrantCalls--;
        return JSR_RUNTIME_ERR;
    }

    vm->reentrantCalls--;
    return JSR_SUCCESS;
}

void jsrEvalBreak(JStarVM* vm) {
    if(vm->frameCount) vm->evalBreak = 1;
}

void jsrPrintStacktrace(JStarVM* vm, int slot) {
    PROFILE_FUNC()

    Value exc = vm->apiStack[apiStackIndex(vm, slot)];
    ASSERT(isInstance(vm, exc, vm->excClass), "Top of stack isn't an exception");
    push(vm, exc);

    // Can fail with a stack overflow (for example if there is a cycle in exception causes)
    if(jsrCallMethod(vm, "printStacktrace", 0) != JSR_SUCCESS) {
        jsrPrintStacktrace(vm, -1);
    }

    pop(vm);
}

void jsrGetStacktrace(JStarVM* vm, int slot) {
    PROFILE_FUNC()

    Value exc = vm->apiStack[apiStackIndex(vm, slot)];
    ASSERT(isInstance(vm, exc, vm->excClass), "Top of stack isn't an exception");
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

    ObjString* stField = copyString(vm, EXC_TRACE, strlen(EXC_TRACE));
    push(vm, OBJ_VAL(stField));

    Value value = NULL_VAL;
    hashTableGet(&exception->fields, stField, &value);
    ObjStackTrace* st = IS_STACK_TRACE(value) ? (ObjStackTrace*)AS_OBJ(value) : newStackTrace(vm);
    st->lastTracedFrame = -1;

    hashTablePut(&exception->fields, stField, OBJ_VAL(st));
    pop(vm);

    // Place the exception on top of the stack if not already
    if(!valueEquals(excVal, vm->sp[-1])) {
        push(vm, excVal);
    }
}

void jsrRaise(JStarVM* vm, const char* cls, const char* err, ...) {
    PROFILE_FUNC()

    if(!jsrGetGlobal(vm, NULL, cls)) return;

    ObjInstance* exception = newInstance(vm, AS_CLASS(pop(vm)));
    if(!isInstance(vm, OBJ_VAL(exception), vm->excClass)) {
        jsrRaise(vm, "TypeException", "Can only raise Exception instances.");
    }

    push(vm, OBJ_VAL(exception));

    ObjStackTrace* st = newStackTrace(vm);
    push(vm, OBJ_VAL(st));
    hashTablePut(&exception->fields, copyString(vm, EXC_TRACE, strlen(EXC_TRACE)), OBJ_VAL(st));
    pop(vm);

    if(err != NULL) {
        JStarBuffer error;
        jsrBufferInit(vm, &error);

        va_list args;
        va_start(args, err);
        jsrBufferAppendvf(&error, err, args);
        va_end(args);

        ObjString* errorField = copyString(vm, EXC_ERR, strlen(EXC_ERR));
        ObjString* errorString = jsrBufferToString(&error);
        hashTablePut(&exception->fields, errorField, OBJ_VAL(errorString));
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

void jsrAddImportPath(JStarVM* vm, const char* path) {
    listAppend(vm, vm->importPaths, OBJ_VAL(copyString(vm, path, strlen(path))));
}

void jsrEnsureStack(JStarVM* vm, size_t needed) {
    reserveStack(vm, needed);
}

bool jsrReadFile(JStarVM* vm, const char* path, JStarBuffer* out) {
    int saveErrno;
    size_t read;

    FILE* src = fopen(path, "rb");
    if(src == NULL) {
        return false;
    }

    char header[SERIALIZED_HEADER_SZ];
    read = fread(header, 1, SERIALIZED_HEADER_SZ, src);
    if(ferror(src)) {
        goto error;
    }

    bool compiled = (read == SERIALIZED_HEADER_SZ) &&
                    (memcmp(SERIALIZED_HEADER, header, read) == 0);

    if(fseek(src, 0, SEEK_END)) {
        goto error;
    }

    long size = ftell(src);
    if(size < 0) goto error;
    rewind(src);

    jsrBufferInitCapacity(vm, out, size + (compiled ? 0 : 1));
    read = fread(out->data, 1, size, src);
    if(read < (size_t)size) {
        saveErrno = errno;
        jsrBufferFree(out);
        errno = saveErrno;
        goto error;
    }

    if(fclose(src)) {
        return false;
    }

    out->size = size;
    if(!compiled) out->data[size] = '\0';
    return src;

error:
    saveErrno = errno;
    if(fclose(src)) return false;
    errno = saveErrno;
    return false;
}

bool jsrRawEquals(JStarVM* vm, int slot1, int slot2) {
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

bool jsrIs(JStarVM* vm, int slot, int classSlot) {
    Value v = apiStackSlot(vm, slot);
    Value cls = apiStackSlot(vm, classSlot);
    return IS_CLASS(cls) ? isInstance(vm, v, AS_CLASS(cls)) : false;
}

static void validateStack(JStarVM* vm) {
    ASSERT((size_t)(vm->sp - vm->stack) <= vm->stackSz, "Stack overflow");
}

void jsrPushNumber(JStarVM* vm, double number) {
    validateStack(vm);
    push(vm, NUM_VAL(number));
}

void jsrPushBoolean(JStarVM* vm, bool boolean) {
    validateStack(vm);
    push(vm, BOOL_VAL(boolean));
}

void jsrPushStringSz(JStarVM* vm, const char* string, size_t length) {
    validateStack(vm);
    push(vm, OBJ_VAL(copyString(vm, string, length)));
}

void jsrPushString(JStarVM* vm, const char* string) {
    jsrPushStringSz(vm, string, strlen(string));
}

void jsrPushHandle(JStarVM* vm, void* handle) {
    validateStack(vm);
    push(vm, HANDLE_VAL(handle));
}

void jsrPushNull(JStarVM* vm) {
    validateStack(vm);
    push(vm, NULL_VAL);
}

void jsrPushList(JStarVM* vm) {
    validateStack(vm);
    push(vm, OBJ_VAL(newList(vm, 16)));
}

void jsrPushTuple(JStarVM* vm, size_t size) {
    validateStack(vm);
    ObjTuple* tup = newTuple(vm, size);
    for(size_t i = 1; i <= size; i++) {
        tup->arr[size - i] = pop(vm);
    }
    push(vm, OBJ_VAL(tup));
}

void jsrPushTable(JStarVM* vm) {
    validateStack(vm);
    push(vm, OBJ_VAL(newTable(vm)));
}

void jsrPushValue(JStarVM* vm, int slot) {
    validateStack(vm);
    push(vm, apiStackSlot(vm, slot));
}

void* jsrPushUserdata(JStarVM* vm, size_t size, void (*finalize)(void*)) {
    validateStack(vm);
    ObjUserdata* udata = newUserData(vm, size, finalize);
    push(vm, OBJ_VAL(udata));
    return (void*)udata->data;
}

void jsrPushNative(JStarVM* vm, const char* module, const char* name, JStarNative nat,
                   uint8_t argc) {
    validateStack(vm);
    ObjModule* mod = getModule(vm, copyString(vm, module, strlen(module)));
    ASSERT(mod, "Cannot find module");

    ObjString* nativeName = copyString(vm, name, strlen(name));
    push(vm, OBJ_VAL(nativeName));
    ObjNative* native = newNative(vm, mod, argc, 0, false);
    native->proto.name = nativeName;
    native->fn = nat;
    pop(vm);

    push(vm, OBJ_VAL(native));
}

void jsrPop(JStarVM* vm) {
    ASSERT(vm->sp > vm->apiStack, "Popping past frame boundary");
    pop(vm);
}

int jsrTop(JStarVM* vm) {
    return apiStackIndex(vm, -1);
}

void jsrSetGlobal(JStarVM* vm, const char* module, const char* name) {
    ObjModule* mod = module ? getModule(vm, copyString(vm, module, strlen(module))) : vm->module;
    ASSERT(mod, "Module doesn't exist");
    hashTablePut(&mod->globals, copyString(vm, name, strlen(name)), peek(vm));
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
    ASSERT(IS_LIST(lst), "Not a list");
    listAppend(vm, AS_LIST(lst), peek(vm));
}

void jsrListInsert(JStarVM* vm, size_t i, int slot) {
    Value lstVal = apiStackSlot(vm, slot);
    ASSERT(IS_LIST(lstVal), "Not a list");
    ObjList* lst = AS_LIST(lstVal);
    ASSERT(i < lst->size, "Out of bounds");
    listInsert(vm, lst, (size_t)i, peek(vm));
}

void jsrListRemove(JStarVM* vm, size_t i, int slot) {
    Value lstVal = apiStackSlot(vm, slot);
    ASSERT(IS_LIST(lstVal), "Not a list");
    ObjList* lst = AS_LIST(lstVal);
    ASSERT(i < lst->size, "Out of bounds");
    listRemove(vm, lst, (size_t)i);
}

void jsrListGet(JStarVM* vm, size_t i, int slot) {
    Value lstVal = apiStackSlot(vm, slot);
    ASSERT(IS_LIST(lstVal), "Not a list");
    ObjList* lst = AS_LIST(lstVal);
    ASSERT(i < lst->size, "Out of bounds");
    push(vm, lst->arr[i]);
}

size_t jsrListGetLength(JStarVM* vm, int slot) {
    Value lst = apiStackSlot(vm, slot);
    ASSERT(IS_LIST(lst), "Not a list");
    return AS_LIST(lst)->size;
}

void jsrTupleGet(JStarVM* vm, size_t i, int slot) {
    Value tupVal = apiStackSlot(vm, slot);
    ASSERT(IS_TUPLE(tupVal), "Not a tuple");
    ObjTuple* tuple = AS_TUPLE(tupVal);
    ASSERT(i < tuple->size, "Out of bounds");
    push(vm, tuple->arr[i]);
}

size_t jsrTupleGetLength(JStarVM* vm, int slot) {
    Value tup = apiStackSlot(vm, slot);
    ASSERT(IS_TUPLE(tup), "Not a tuple");
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
    return setValueField(vm, copyString(vm, name, strlen(name)));
}

bool jsrGetField(JStarVM* vm, int slot, const char* name) {
    push(vm, apiStackSlot(vm, slot));
    return getValueField(vm, copyString(vm, name, strlen(name)));
}

bool jsrGetGlobal(JStarVM* vm, const char* module, const char* name) {
    ObjModule* mod = module ? getModule(vm, copyString(vm, module, strlen(module))) : vm->module;
    ASSERT(mod, "Module doesn't exist");

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
    ASSERT(IS_CLASS(cls), "clsSlot is not a Class");
    ASSERT(IS_NATIVE(nat), "natSlot is not a Native Function");
    hashTablePut(&AS_CLASS(cls)->methods, AS_NATIVE(nat)->proto.name, nat);
}

void* jsrGetUserdata(JStarVM* vm, int slot) {
    ASSERT(IS_USERDATA(apiStackSlot(vm, slot)), "slot is not a Userdatum");
    return (void*)AS_USERDATA(apiStackSlot(vm, slot))->data;
}

double jsrGetNumber(JStarVM* vm, int slot) {
    ASSERT(IS_NUM(apiStackSlot(vm, slot)), "slot is not a Number");
    return AS_NUM(apiStackSlot(vm, slot));
}

const char* jsrGetString(JStarVM* vm, int slot) {
    ASSERT(IS_STRING(apiStackSlot(vm, slot)), "slot is not a String");
    return AS_STRING(apiStackSlot(vm, slot))->data;
}

size_t jsrGetStringSz(JStarVM* vm, int slot) {
    ASSERT(IS_STRING(apiStackSlot(vm, slot)), "slot is not a String");
    return AS_STRING(apiStackSlot(vm, slot))->length;
}

bool jsrGetBoolean(JStarVM* vm, int slot) {
    ASSERT(IS_BOOL(apiStackSlot(vm, slot)), "slot is not a Boolean");
    return AS_BOOL(apiStackSlot(vm, slot));
}

void* jsrGetHandle(JStarVM* vm, int slot) {
    ASSERT(IS_HANDLE(apiStackSlot(vm, slot)), "slot is not an Handle");
    return AS_HANDLE(apiStackSlot(vm, slot));
}

bool jsrIsNumber(JStarVM* vm, int slot) {
    return IS_NUM(apiStackSlot(vm, slot));
}

bool jsrIsInteger(JStarVM* vm, int slot) {
    return IS_INT(apiStackSlot(vm, slot));
}

bool jsrIsString(JStarVM* vm, int slot) {
    return IS_STRING(apiStackSlot(vm, slot));
}

bool jsrIsList(JStarVM* vm, int slot) {
    return IS_LIST(apiStackSlot(vm, slot));
}

bool jsrIsTuple(JStarVM* vm, int slot) {
    return IS_TUPLE(apiStackSlot(vm, slot));
}

bool jsrIsBoolean(JStarVM* vm, int slot) {
    return IS_BOOL(apiStackSlot(vm, slot));
}

bool jsrIsNull(JStarVM* vm, int slot) {
    return IS_NULL(apiStackSlot(vm, slot));
}

bool jsrIsInstance(JStarVM* vm, int slot) {
    return IS_INSTANCE(apiStackSlot(vm, slot));
}

bool jsrIsHandle(JStarVM* vm, int slot) {
    return IS_HANDLE(apiStackSlot(vm, slot));
}

bool jsrIsTable(JStarVM* vm, int slot) {
    return IS_TABLE(apiStackSlot(vm, slot));
}

bool jsrIsFunction(JStarVM* vm, int slot) {
    Value val = apiStackSlot(vm, slot);
    return IS_CLOSURE(val) || IS_NATIVE(val) || IS_BOUND_METHOD(val);
}

bool jsrIsUserdata(JStarVM* vm, int slot) {
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
    if(!jsrIsBoolean(vm, slot)) JSR_RAISE(vm, "TypeException", "%s must be a String.", name);
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
