#include "jstar.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "const.h"
#include "hashtable.h"
#include "import.h"
#include "jsrparse/ast.h"
#include "jsrparse/parser.h"
#include "memory.h"
#include "object.h"
#include "util.h"
#include "value.h"
#include "vm.h"

/**
 * The bulk of the API (jstar.h) implementation.
 *
 * JStarBuffer is implemented in object.c
 */

EvalResult jsrEvaluate(JStarVM *vm, const char *fpath, const char *src) {
    return jsrEvaluateModule(vm, fpath, JSR_MAIN_MODULE, src);
}

EvalResult jsrEvaluateModule(JStarVM *vm, const char *fpath, const char *module, const char *src) {
    Stmt *program = parse(fpath, src);
    if(program == NULL) return VM_SYNTAX_ERR;

    ObjString *name = copyString(vm, module, strlen(module), true);
    ObjFunction *fn = compileWithModule(vm, name, program);
    freeStmt(program);

    if(fn == NULL) return VM_COMPILE_ERR;

    push(vm, OBJ_VAL(fn));
    ObjClosure *closure = newClosure(vm, fn);
    pop(vm);

    push(vm, OBJ_VAL(closure));

    EvalResult res;
    if((res = jsrCall(vm, 0)) != VM_EVAL_SUCCESS) {
        jsrPrintStacktrace(vm, -1);
    }

    pop(vm);
    return res;
}

static EvalResult finishCall(JStarVM *vm, int depth, size_t offSp) {
    // Evaluate frame if present
    if(vm->frameCount > depth && !runEval(vm, depth)) {
        // Exception was thrown, push it as result
        Value exc = pop(vm);
        vm->sp = vm->stack + offSp;
        push(vm, exc);
        return VM_RUNTIME_ERR;
    }
    return VM_EVAL_SUCCESS;
}

static void callError(JStarVM *vm, int depth, size_t offsp) {
    // Finish to unwind the stack
    if(vm->frameCount > depth) {
        unwindStack(vm, depth);
        Value exc = pop(vm);
        vm->sp = vm->stack + offsp;
        push(vm, exc);
    }
}

EvalResult jsrCall(JStarVM *vm, uint8_t argc) {
    size_t offsp = vm->sp - vm->stack - argc - 1;
    int depth = vm->frameCount;

    if(!callValue(vm, peekn(vm, argc), argc)) {
        callError(vm, depth, offsp);
        return VM_RUNTIME_ERR;
    }

    return finishCall(vm, depth, offsp);
}

EvalResult jsrCallMethod(JStarVM *vm, const char *name, uint8_t argc) {
    size_t offsp = vm->sp - vm->stack - argc - 1;
    int depth = vm->frameCount;

    if(!invokeValue(vm, copyString(vm, name, strlen(name), true), argc)) {
        callError(vm, depth, offsp);
        return VM_RUNTIME_ERR;
    }

    return finishCall(vm, depth, offsp);
}

void jsrPrintStacktrace(JStarVM *vm, int slot) {
    Value exc = vm->apiStack[apiStackIndex(vm, slot)];
    assert(isInstance(vm, exc, vm->excClass), "Top of stack isn't an exception");
    push(vm, exc);
    jsrCallMethod(vm, "printStacktrace", 0);
    jsrPop(vm);
}

void jsrRaiseException(JStarVM *vm, int slot) {
    Value exc = apiStackSlot(vm, slot);
    if(!isInstance(vm, exc, vm->excClass)) {
        jsrRaise(vm, "TypeException", "Can only raise Exception instances.");
        return;
    }
    ObjInstance *objException = (ObjInstance *)AS_OBJ(exc);
    ObjStackTrace *staktrace = newStackTrace(vm);
    hashTablePut(&objException->fields, vm->stacktrace, OBJ_VAL(staktrace));
    jsrPushValue(vm, slot);
}

void jsrRaise(JStarVM *vm, const char *cls, const char *err, ...) {
    if(!jsrGetGlobal(vm, NULL, cls)) return;

    ObjInstance *excInst = newInstance(vm, AS_CLASS(pop(vm)));
    if(!isInstance(vm, OBJ_VAL(excInst), vm->excClass)) {
        jsrRaise(vm, "TypeException", "Can only raise Exception instances.");
    }

    push(vm, OBJ_VAL(excInst));
    ObjStackTrace *st = newStackTrace(vm);
    hashTablePut(&excInst->fields, vm->stacktrace, OBJ_VAL(st));

    if(err != NULL) {
        char errStr[512] = {0};
        va_list args;
        va_start(args, err);
        vsnprintf(errStr, sizeof(errStr) - 1, err, args);
        va_end(args);

        jsrPushString(vm, errStr);
        HashTable *fields = &excInst->fields;
        hashTablePut(fields, copyString(vm, EXC_M_ERR, strlen(EXC_M_ERR), true), pop(vm));
    }
}

void jsrInitCommandLineArgs(JStarVM *vm, int argc, const char **argv) {
    vm->argc = argc;
    vm->argv = argv;
}

void jsrAddImportPath(JStarVM *vm, const char *path) {
    listAppend(vm, vm->importpaths, OBJ_VAL(copyString(vm, path, strlen(path), false)));
}

void jsrEnsureStack(JStarVM *vm, size_t needed) {
    if(vm->sp + needed < vm->stack + vm->stackSz) return;

    Value *oldStack = vm->stack;

    vm->stackSz = powerOf2Ceil(vm->stackSz);
    vm->stack = realloc(vm->stack, sizeof(Value) * vm->stackSz);

    if(vm->stack != oldStack) {
        if(vm->apiStack >= vm->stack && vm->apiStack <= vm->sp) {
            vm->apiStack = vm->stack + (vm->apiStack - oldStack);
        }

        for(int i = 0; i < vm->frameCount; i++) {
            Frame *frame = &vm->frames[i];
            frame->stack = vm->stack + (frame->stack - oldStack);
            for(int j = 0; j < frame->handlerc; j++) {
                Handler *h = &frame->handlers[j];
                h->savesp = vm->stack + (h->savesp - oldStack);
            }
        }

        ObjUpvalue *upvalue = vm->upvalues;
        while(upvalue) {
            upvalue->addr = vm->stack + (upvalue->addr - oldStack);
            upvalue = upvalue->next;
        }

        vm->sp = vm->stack + (vm->sp - oldStack);
    }
}

char *jsrReadFile(const char *path) {
    FILE *srcFile = fopen(path, "rb");
    if(srcFile == NULL || errno == EISDIR) {
        if(srcFile) fclose(srcFile);
        return NULL;
    }

    fseek(srcFile, 0, SEEK_END);
    size_t size = ftell(srcFile);
    rewind(srcFile);

    char *src = malloc(size + 1);
    if(src == NULL) {
        fclose(srcFile);
        return NULL;
    }

    size_t read = fread(src, sizeof(char), size, srcFile);
    if(read < size) {
        free(src);
        fclose(srcFile);
        return NULL;
    }

    fclose(srcFile);
    src[read] = '\0';
    return src;
}

static void validateStack(JStarVM *vm) {
    assert((size_t)(vm->sp - vm->stack) <= vm->stackSz, "Stack overflow");
}

bool jsrRawEquals(JStarVM *vm, int slot1, int slot2) {
    Value v1 = apiStackSlot(vm, slot1);
    Value v2 = apiStackSlot(vm, slot2);
    return valueEquals(v1, v2);
}

bool jsrEquals(JStarVM *vm) {
    if(IS_NUM(peek2(vm)) || IS_NULL(peek2(vm)) || IS_BOOL(peek2(vm))) {
        push(vm, BOOL_VAL(valueEquals(pop(vm), pop(vm))));
        return true;
    } else {
        ObjClass *cls = getClass(vm, peek2(vm));
        Value eq;
        if(hashTableGet(&cls->methods, vm->eq, &eq)) {
            return jsrCallMethod(vm, "__eq__", 1) == VM_EVAL_SUCCESS;
        } else {
            push(vm, BOOL_VAL(valueEquals(pop(vm), pop(vm))));
            return true;
        }
    }
}

bool jsrIs(JStarVM *vm, int slot, int classSlot) {
    Value v = apiStackSlot(vm, slot);
    Value cls = apiStackSlot(vm, classSlot);
    if(!IS_CLASS(cls)) return false;
    return isInstance(vm, v, AS_CLASS(cls));
}

bool jsrIter(JStarVM *vm, int iterable, int res, bool *err) {
    jsrEnsureStack(vm, 2);
    jsrPushValue(vm, iterable);
    jsrPushValue(vm, res < 0 ? res - 1 : res);

    if(jsrCallMethod(vm, "__iter__", 1) != VM_EVAL_SUCCESS) {
        return *err = true;
    }
    if(jsrIsNull(vm, -1) || (jsrIsBoolean(vm, -1) && !jsrGetBoolean(vm, -1))) {
        jsrPop(vm);
        return false;
    }

    Value resVal = pop(vm);
    vm->apiStack[apiStackIndex(vm, res)] = resVal;
    return true;
}

bool jsrNext(JStarVM *vm, int iterable, int res) {
    jsrPushValue(vm, iterable);
    jsrPushValue(vm, res < 0 ? res - 1 : res);
    if(jsrCallMethod(vm, "__next__", 1) != VM_EVAL_SUCCESS) return false;
    return true;
}

void jsrPushNumber(JStarVM *vm, double number) {
    validateStack(vm);
    push(vm, NUM_VAL(number));
}

void jsrPushBoolean(JStarVM *vm, bool boolean) {
    validateStack(vm);
    push(vm, BOOL_VAL(boolean));
}

void jsrPushStringSz(JStarVM *vm, const char *string, size_t length) {
    validateStack(vm);
    push(vm, OBJ_VAL(copyString(vm, string, length, false)));
}
void jsrPushString(JStarVM *vm, const char *string) {
    jsrPushStringSz(vm, string, strlen(string));
}

void jsrPushHandle(JStarVM *vm, void *handle) {
    validateStack(vm);
    push(vm, HANDLE_VAL(handle));
}

void jsrPushNull(JStarVM *vm) {
    validateStack(vm);
    push(vm, NULL_VAL);
}

void jsrPushList(JStarVM *vm) {
    validateStack(vm);
    push(vm, OBJ_VAL(newList(vm, 16)));
}

void jsrPushTuple(JStarVM *vm, size_t size) {
    validateStack(vm);
    ObjTuple *tup = newTuple(vm, size);
    for(size_t i = 1; i <= size; i++) {
        tup->arr[size - i] = pop(vm);
    }
    push(vm, OBJ_VAL(tup));
}

void jsrPushTable(JStarVM *vm) {
    validateStack(vm);
    push(vm, OBJ_VAL(newTable(vm)));
}

void jsrPushValue(JStarVM *vm, int slot) {
    validateStack(vm);
    push(vm, apiStackSlot(vm, slot));
}

void jsrPushNative(JStarVM *vm, const char *name, JStarNative nat, uint8_t argc) {
    validateStack(vm);

    ObjModule *mod;
    if(vm->module) {
        mod = vm->module;
    } else {
        mod = getModule(vm, copyString(vm, JSR_MAIN_MODULE, strlen(JSR_MAIN_MODULE), true));
    }

    ObjString *objName = copyString(vm, name, strlen(name), true);
    push(vm, OBJ_VAL(objName));
    ObjNative *objNative = newNative(vm, mod, objName, argc, nat, 0);
    pop(vm);

    push(vm, OBJ_VAL(objNative));
}

void *jsrPushUserdata(JStarVM *vm, size_t size, void (*finalize)(void *)) {
    validateStack(vm);
    ObjUserdata *udata = newUserData(vm, size, finalize);
    push(vm, OBJ_VAL(udata));
    return (void *)udata->data;
}

void jsrPop(JStarVM *vm) {
    assert(vm->sp > vm->apiStack, "Popping past frame boundary");
    pop(vm);
}

void jsrSetGlobal(JStarVM *vm, const char *mname, const char *name) {
    assert(vm->module || mname,
           "Calling jsrSetGlobal outside of Native function requires specifying a module");
    ObjModule *module =
        mname ? getModule(vm, copyString(vm, mname, strlen(mname), true)) : vm->module;
    hashTablePut(&module->globals, copyString(vm, name, strlen(name), true), peek(vm));
}

void jsrListAppend(JStarVM *vm, int slot) {
    Value lst = apiStackSlot(vm, slot);
    assert(IS_LIST(lst), "Not a list");
    listAppend(vm, AS_LIST(lst), peek(vm));
}

void jsrListInsert(JStarVM *vm, size_t i, int slot) {
    Value lstVal = apiStackSlot(vm, slot);
    assert(IS_LIST(lstVal), "Not a list");
    ObjList *lst = AS_LIST(lstVal);
    assert(i < lst->count, "Out of bounds");
    listInsert(vm, lst, (size_t)i, peek(vm));
}

void jsrListRemove(JStarVM *vm, size_t i, int slot) {
    Value lstVal = apiStackSlot(vm, slot);
    assert(IS_LIST(lstVal), "Not a list");
    ObjList *lst = AS_LIST(lstVal);
    assert(i < lst->count, "Out of bounds");
    listRemove(vm, lst, (size_t)i);
}

void jsrListGet(JStarVM *vm, size_t i, int slot) {
    Value lstVal = apiStackSlot(vm, slot);
    assert(IS_LIST(lstVal), "Not a list");
    ObjList *lst = AS_LIST(lstVal);
    assert(i < lst->count, "Out of bounds");
    push(vm, lst->arr[i]);
}

size_t jsrListGetLength(JStarVM *vm, int slot) {
    Value lst = apiStackSlot(vm, slot);
    assert(IS_LIST(lst), "Not a list");
    return AS_LIST(lst)->count;
}

void jsrTupleGet(JStarVM *vm, size_t i, int slot) {
    Value tupVal = apiStackSlot(vm, slot);
    assert(IS_TUPLE(tupVal), "Not a tuple");
    ObjTuple *tuple = AS_TUPLE(tupVal);
    assert(i < tuple->size, "Out of bounds");
    push(vm, tuple->arr[i]);
}

size_t jsrTupleGetLength(JStarVM *vm, int slot) {
    Value tup = apiStackSlot(vm, slot);
    assert(IS_TUPLE(tup), "Not a tuple");
    return AS_TUPLE(tup)->size;
}

void jsrSetField(JStarVM *vm, int slot, const char *name) {
    Value val = apiStackSlot(vm, slot);
    setFieldOfValue(vm, val, copyString(vm, name, strlen(name), true), peek(vm));
}

bool jsrGetField(JStarVM *vm, int slot, const char *name) {
    Value val = apiStackSlot(vm, slot);
    return getFieldFromValue(vm, val, copyString(vm, name, strlen(name), true));
}

bool jsrGetGlobal(JStarVM *vm, const char *mname, const char *name) {
    assert(vm->module || mname,
           "Calling jsrGetGlobal outside of Native function requires specifying a module");
    ObjModule *module =
        mname ? getModule(vm, copyString(vm, mname, strlen(mname), true)) : vm->module;

    Value res;
    ObjString *namestr = copyString(vm, name, strlen(name), true);
    HashTable *glob = &module->globals;
    if(!hashTableGet(glob, namestr, &res)) {
        jsrRaise(vm, "NameException", "Name %s not definied in module %s.", name, mname);
        return false;
    }

    push(vm, res);
    return true;
}

void *jsrGetUserdata(JStarVM *vm, int slot) {
    return (void *)AS_USERDATA(apiStackSlot(vm, slot))->data;
}

double jsrGetNumber(JStarVM *vm, int slot) {
    return AS_NUM(apiStackSlot(vm, slot));
}

const char *jsrGetString(JStarVM *vm, int slot) {
    return AS_STRING(apiStackSlot(vm, slot))->data;
}

size_t jsrGetStringSz(JStarVM *vm, int slot) {
    return AS_STRING(apiStackSlot(vm, slot))->length;
}

bool jsrGetBoolean(JStarVM *vm, int slot) {
    return AS_BOOL(apiStackSlot(vm, slot));
}

void *jsrGetHandle(JStarVM *vm, int slot) {
    return AS_HANDLE(apiStackSlot(vm, slot));
}

bool jsrIsNumber(JStarVM *vm, int slot) {
    return IS_NUM(apiStackSlot(vm, slot));
}

bool jsrIsInteger(JStarVM *vm, int slot) {
    return IS_INT(apiStackSlot(vm, slot));
}

bool jsrIsString(JStarVM *vm, int slot) {
    return IS_STRING(apiStackSlot(vm, slot));
}

bool jsrIsList(JStarVM *vm, int slot) {
    return IS_LIST(apiStackSlot(vm, slot));
}

bool jsrIsTuple(JStarVM *vm, int slot) {
    return IS_TUPLE(apiStackSlot(vm, slot));
}

bool jsrIsBoolean(JStarVM *vm, int slot) {
    return IS_BOOL(apiStackSlot(vm, slot));
}

bool jsrIsNull(JStarVM *vm, int slot) {
    return IS_NULL(apiStackSlot(vm, slot));
}

bool jsrIsInstance(JStarVM *vm, int slot) {
    return IS_INSTANCE(apiStackSlot(vm, slot));
}

bool jsrIsHandle(JStarVM *vm, int slot) {
    return IS_HANDLE(apiStackSlot(vm, slot));
}

bool jsrIsTable(JStarVM *vm, int slot) {
    return IS_TABLE(apiStackSlot(vm, slot));
}

bool jsrIsFunction(JStarVM *vm, int slot) {
    Value val = apiStackSlot(vm, slot);
    return IS_CLOSURE(val) || IS_NATIVE(val) || IS_BOUND_METHOD(val);
}

bool jsrIsUserdata(JStarVM *vm, int slot) {
    Value val = apiStackSlot(vm, slot);
    return IS_USERDATA(val);
}

bool jsrCheckNumber(JStarVM *vm, int slot, const char *name) {
    if(!jsrIsNumber(vm, slot)) JSR_RAISE(vm, "TypeException", "%s must be a number.", name);
    return true;
}

bool jsrCheckInt(JStarVM *vm, int slot, const char *name) {
    if(!jsrIsInteger(vm, slot)) JSR_RAISE(vm, "TypeException", "%s must be an integer.", name);
    return true;
}

bool jsrCheckString(JStarVM *vm, int slot, const char *name) {
    if(!jsrIsString(vm, slot)) JSR_RAISE(vm, "TypeException", "%s must be a String.", name);
    return true;
}

bool jsrCheckList(JStarVM *vm, int slot, const char *name) {
    if(!jsrIsList(vm, slot)) JSR_RAISE(vm, "TypeException", "%s must be a List.", name);
    return true;
}

bool jsrCheckTuple(JStarVM *vm, int slot, const char *name) {
    if(!jsrIsTuple(vm, slot)) JSR_RAISE(vm, "TypeException", "%s must be a Tuple.", name);
    return true;
}

bool jsrCheckBoolean(JStarVM *vm, int slot, const char *name) {
    if(!jsrIsBoolean(vm, slot)) JSR_RAISE(vm, "TypeException", "%s must be a String.", name);
    return true;
}

bool jsrCheckInstance(JStarVM *vm, int slot, const char *name) {
    if(!jsrIsInstance(vm, slot)) JSR_RAISE(vm, "TypeException", "%s must be an instance.", name);
    return true;
}

bool jsrCheckHandle(JStarVM *vm, int slot, const char *name) {
    if(!jsrIsHandle(vm, slot)) JSR_RAISE(vm, "TypeException", "%s must be an Handle.", name);
    return true;
}

bool jsrCheckTable(JStarVM *vm, int slot, const char *name) {
    if(!jsrIsTable(vm, slot)) JSR_RAISE(vm, "TypeException", "%s must be a Table.", name);
    return true;
}

bool jsrCheckFunction(JStarVM *vm, int slot, const char *name) {
    if(!jsrIsFunction(vm, slot)) JSR_RAISE(vm, "TypeException", "%s must be a Function.", name);
    return true;
}

bool jsrCheckUserdata(JStarVM *vm, int slot, const char *name) {
    if(!jsrIsUserdata(vm, slot)) JSR_RAISE(vm, "TypeException", "%s must be a Userdata.", name);
    return true;
}

size_t jsrCheckIndexNum(JStarVM *vm, double i, size_t max) {
    if(i >= 0 && i < max) return (size_t)i;
    jsrRaise(vm, "IndexOutOfBoundException", "%g.", i);
    return SIZE_MAX;
}

size_t jsrCheckIndex(JStarVM *vm, int slot, size_t max, const char *name) {
    if(!jsrCheckInt(vm, slot, name)) return SIZE_MAX;
    double i = jsrGetNumber(vm, slot);
    return jsrCheckIndexNum(vm, i, max);
}
