#include "jstar.h"
#include "import.h"
#include "memory.h"
#include "util.h"
#include "vm.h"

#include <string.h>

/**
 * The bulk of the API (jstar.h) implementation.
 * 
 * The VM entry point functions and others that need to manipulate the VM internals are implemented 
 * in vm.c
 * 
 * The blBuffer functions are implemented in memory.c since they require garbage collectable 
 * memory allocation
 */

static void validateStack(JStarVM *vm) {
    assert((size_t)(vm->sp - vm->stack) <= vm->stackSz, "Stack overflow");
}

static size_t checkIndex(JStarVM *vm, double i, size_t max, const char *name) {
    if(i >= 0 && i < max) return (size_t)i;
    jsrRaise(vm, "IndexOutOfBoundException", "%g.", i);
    return SIZE_MAX;
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

void jsrPushValue(JStarVM *vm, int slot) {
    validateStack(vm);
    push(vm, apiStackSlot(vm, slot));
}

void jsrPop(JStarVM *vm) {
    assert(vm->sp > vm->apiStack, "Popping past frame boundary");
    pop(vm);
}

void jsrSetGlobal(JStarVM *vm, const char *mname, const char *name) {
    assert(vm->module != NULL || mname != NULL,
           "Calling blSetGlobal outside of Native function requires specifying a module");
    ObjModule *module = vm->module;
    if(mname != NULL) {
        module = getModule(vm, copyString(vm, mname, strlen(mname), true));
    }
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

void jsrListGetLength(JStarVM *vm, int slot) {
    Value lst = apiStackSlot(vm, slot);
    assert(IS_LIST(lst), "Not a list");
    push(vm, NUM_VAL((double)AS_LIST(lst)->count));
}

void jsrTupleGetLength(JStarVM *vm, int slot) {
    Value tup = apiStackSlot(vm, slot);
    assert(IS_TUPLE(tup), "Not a tuple");
    push(vm, NUM_VAL((double)AS_TUPLE(tup)->size));
}

void jsrTupleGet(JStarVM *vm, size_t i, int slot) {
    Value tupVal = apiStackSlot(vm, slot);
    assert(IS_TUPLE(tupVal), "Not a tuple");
    ObjTuple *tuple = AS_TUPLE(tupVal);
    assert(i < tuple->size, "Out of bounds");
    push(vm, tuple->arr[i]);
}

bool jsrGetGlobal(JStarVM *vm, const char *mname, const char *name) {
    assert(vm->module != NULL || mname != NULL,
           "Calling blGetGlobal outside of Native function requires specifying a module");

    ObjModule *module = vm->module;
    if(mname != NULL) {
        module = getModule(vm, copyString(vm, mname, strlen(mname), true));
    }
    ObjString *namestr = copyString(vm, name, strlen(name), true);

    Value res;
    if(!hashTableGet(&module->globals, namestr, &res)) {
        if(!hashTableGet(&vm->core->globals, namestr, &res)) {
            jsrRaise(vm, "NameException", "Name %s not definied in module %s.", name, mname);
            return false;
        }
    }

    push(vm, res);
    return true;
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

bool jsrCheckNum(JStarVM *vm, int slot, const char *name) {
    if(!jsrIsNumber(vm, slot)) JSR_RAISE(vm, "TypeException", "%s must be a number.", name);
    return true;
}

bool jsrCheckInt(JStarVM *vm, int slot, const char *name) {
    if(!jsrIsInteger(vm, slot)) JSR_RAISE(vm, "TypeException", "%s must be an integer.", name);
    return true;
}

bool jsrCheckStr(JStarVM *vm, int slot, const char *name) {
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

bool jsrCheckBool(JStarVM *vm, int slot, const char *name) {
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

size_t jsrCheckIndex(JStarVM *vm, int slot, size_t max, const char *name) {
    if(!jsrCheckInt(vm, slot, name)) return SIZE_MAX;
    double i = jsrGetNumber(vm, slot);
    return checkIndex(vm, i, max, name);
}

void jsrPrintStackTrace(JStarVM *vm) {
    assert(IS_INSTANCE(peek(vm)), "Top of stack isnt't an exception");

    ObjInstance *exc = AS_INSTANCE(peek(vm));

    Value stval = NULL_VAL;
    hashTableGet(&exc->fields, vm->stField, &stval);
    assert(IS_STACK_TRACE(stval), "Top of stack isn't an exception");

    ObjStackTrace *st = AS_STACK_TRACE(stval);

    // Print stacktrace in reverse order of recording (most recent call last)
    if(st->stacktrace.len > 0) {
        fprintf(stderr, "Traceback (most recent call last):\n");

        char *stacktrace = st->stacktrace.data;
        int lastnl = st->stacktrace.len;
        for(int i = lastnl - 1; i > 0; i--) {
            if(stacktrace[i - 1] == '\n') {
                fprintf(stderr, "    %.*s", lastnl - i, stacktrace + i);
                lastnl = i;
            }
        }
        fprintf(stderr, "    %.*s", lastnl, stacktrace);
    }

    // print the exception instance information
    Value v;
    bool found = hashTableGet(&exc->fields, copyString(vm, "err", 3, true), &v);

    if(found && IS_STRING(v)) {
        fprintf(stderr, "%s: %s\n", exc->base.cls->name->data, AS_STRING(v)->data);
    } else {
        fprintf(stderr, "%s\n", exc->base.cls->name->data);
    }
}