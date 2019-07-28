#include "blang.h"
#include "import.h"
#include "memory.h"
#include "util.h"
#include "vm.h"

#include <string.h>

/**
 * The bulk of the API (blang.h) implementation.
 * 
 * The VM entry point functions and others that need to manipulate the VM internals are implemented 
 * in vm.c
 * 
 * The blBuffer functions are implemented in memory.c since they require garbage collectable 
 * memory allocation
 */

static void validateStack(BlangVM *vm) {
    assert((size_t)(vm->sp - vm->stack) <= vm->stackSz, "Stack overflow");
}

static size_t checkIndex(BlangVM *vm, double i, size_t max, const char *name) {
    if(i >= 0 && i < max) return (size_t)i;
    blRaise(vm, "IndexOutOfBoundException", "%g.", i);
    return SIZE_MAX;
}

bool blEquals(BlangVM *vm) {
    if(IS_NUM(peek2(vm)) || IS_NULL(peek2(vm)) || IS_BOOL(peek2(vm))) {
        push(vm, BOOL_VAL(valueEquals(pop(vm), pop(vm))));
        return true;
    } else {
        ObjClass *cls = getClass(vm, peek2(vm));
        Value eq;
        if(hashTableGet(&cls->methods, vm->eq, &eq)) {
            return blCallMethod(vm, "__eq__", 1) == VM_EVAL_SUCCSESS;
        } else {
            push(vm, BOOL_VAL(valueEquals(pop(vm), pop(vm))));
            return true;
        }
    }
}

bool blIs(BlangVM *vm, int slot, int classSlot) {
    Value v = apiStackSlot(vm, slot);
    Value cls = apiStackSlot(vm, classSlot);
    if(!IS_CLASS(cls)) return false;
    return isInstance(vm, v, AS_CLASS(cls));
}

bool blIter(BlangVM *vm, int iterable, int res, bool *err) {
    ensureStack(vm, 2);
    blPushValue(vm, iterable);
    blPushValue(vm, res < 0 ? res - 1 : res);

    if(blCallMethod(vm, "__iter__", 1) != VM_EVAL_SUCCSESS) {
        return *err = true;
    }
    if(blIsNull(vm, -1) || (blIsBoolean(vm, -1) && !blGetBoolean(vm, -1))) {
        blPop(vm);
        return false;
    }

    Value resVal = pop(vm);
    vm->apiStack[apiStackIndex(vm, res)] = resVal;
    return true;
}

bool blNext(BlangVM *vm, int iterable, int res) {
    blPushValue(vm, iterable);
    blPushValue(vm, res < 0 ? res - 1 : res);
    if(blCallMethod(vm, "__next__", 1) != VM_EVAL_SUCCSESS) return false;
    return true;
}

void blPushNumber(BlangVM *vm, double number) {
    validateStack(vm);
    push(vm, NUM_VAL(number));
}

void blPushBoolean(BlangVM *vm, bool boolean) {
    validateStack(vm);
    push(vm, BOOL_VAL(boolean));
}

void blPushStringSz(BlangVM *vm, const char *string, size_t length) {
    validateStack(vm);
    push(vm, OBJ_VAL(copyString(vm, string, length, false)));
}
void blPushString(BlangVM *vm, const char *string) {
    blPushStringSz(vm, string, strlen(string));
}

void pushBoolean(BlangVM *vm, bool b) {
    validateStack(vm);
    push(vm, b ? TRUE_VAL : FALSE_VAL);
}

void blPushHandle(BlangVM *vm, void *handle) {
    validateStack(vm);
    push(vm, HANDLE_VAL(handle));
}

void blPushNull(BlangVM *vm) {
    validateStack(vm);
    push(vm, NULL_VAL);
}

void blPushList(BlangVM *vm) {
    validateStack(vm);
    push(vm, OBJ_VAL(newList(vm, 16)));
}

BLANG_API void blPushTuple(BlangVM *vm, size_t size) {
    validateStack(vm);
    ObjTuple *tup = newTuple(vm, size);
    for(size_t i = size; i > 0; i--) {
        tup->arr[size - i] = pop(vm);
    }
    push(vm, OBJ_VAL(tup));
}

void blPushValue(BlangVM *vm, int slot) {
    validateStack(vm);
    push(vm, apiStackSlot(vm, slot));
}

void blPop(BlangVM *vm) {
    assert(vm->sp > vm->apiStack, "Popping past frame boundary");
    pop(vm);
}

void blSetGlobal(BlangVM *vm, const char *mname, const char *name) {
    assert(vm->module != NULL || mname != NULL,
           "Calling blSetGlobal outside of Native function requires specifying a module");
    ObjModule *module = vm->module;
    if(mname != NULL) {
        module = getModule(vm, copyString(vm, mname, strlen(mname), true));
    }
    hashTablePut(&module->globals, copyString(vm, name, strlen(name), true), peek(vm));
}

void blListAppend(BlangVM *vm, int slot) {
    Value lst = apiStackSlot(vm, slot);
    assert(IS_LIST(lst), "Not a list");
    listAppend(vm, AS_LIST(lst), peek(vm));
}

void blListInsert(BlangVM *vm, size_t i, int slot) {
    Value lstVal = apiStackSlot(vm, slot);
    assert(IS_LIST(lstVal), "Not a list");
    ObjList *lst = AS_LIST(lstVal);
    assert(i < lst->count, "Out of bounds");
    listInsert(vm, lst, (size_t)i, peek(vm));
}

void blListRemove(BlangVM *vm, size_t i, int slot) {
    Value lstVal = apiStackSlot(vm, slot);
    assert(IS_LIST(lstVal), "Not a list");
    ObjList *lst = AS_LIST(lstVal);
    assert(i < lst->count, "Out of bounds");
    listRemove(vm, lst, (size_t)i);
}

void blListGet(BlangVM *vm, size_t i, int slot) {
    Value lstVal = apiStackSlot(vm, slot);
    assert(IS_LIST(lstVal), "Not a list");
    ObjList *lst = AS_LIST(lstVal);
    assert(i < lst->count, "Out of bounds");
    push(vm, lst->arr[(size_t)i]);
}

void blListGetLength(BlangVM *vm, int slot) {
    Value lst = apiStackSlot(vm, slot);
    assert(IS_LIST(lst), "Not a list");
    push(vm, NUM_VAL((double)AS_LIST(lst)->count));
}

bool blGetGlobal(BlangVM *vm, const char *mname, const char *name) {
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
            blRaise(vm, "NameException", "Name %s not definied in module %s.", name, mname);
            return false;
        }
    }

    push(vm, res);
    return true;
}

double blGetNumber(BlangVM *vm, int slot) {
    return AS_NUM(apiStackSlot(vm, slot));
}

const char *blGetString(BlangVM *vm, int slot) {
    return AS_STRING(apiStackSlot(vm, slot))->data;
}

size_t blGetStringSz(BlangVM *vm, int slot) {
    return AS_STRING(apiStackSlot(vm, slot))->length;
}

bool blGetBoolean(BlangVM *vm, int slot) {
    return AS_BOOL(apiStackSlot(vm, slot));
}

void *blGetHandle(BlangVM *vm, int slot) {
    return AS_HANDLE(apiStackSlot(vm, slot));
}

bool blIsNumber(BlangVM *vm, int slot) {
    return IS_NUM(apiStackSlot(vm, slot));
}

bool blIsInteger(BlangVM *vm, int slot) {
    return IS_INT(apiStackSlot(vm, slot));
}

bool blIsString(BlangVM *vm, int slot) {
    return IS_STRING(apiStackSlot(vm, slot));
}

bool blIsList(BlangVM *vm, int slot) {
    return IS_LIST(apiStackSlot(vm, slot));
}

bool blIsTuple(BlangVM *vm, int slot) {
    return IS_TUPLE(apiStackSlot(vm, slot));
}

bool blIsBoolean(BlangVM *vm, int slot) {
    return IS_BOOL(apiStackSlot(vm, slot));
}

bool blIsNull(BlangVM *vm, int slot) {
    return IS_NULL(apiStackSlot(vm, slot));
}

bool blIsInstance(BlangVM *vm, int slot) {
    return IS_INSTANCE(apiStackSlot(vm, slot));
}

bool blIsHandle(BlangVM *vm, int slot) {
    return IS_HANDLE(apiStackSlot(vm, slot));
}

bool blCheckNum(BlangVM *vm, int slot, const char *name) {
    if(!blIsNumber(vm, slot)) BL_RAISE(vm, "TypeException", "%s must be a number.", name);
    return true;
}

bool blCheckInt(BlangVM *vm, int slot, const char *name) {
    if(!blIsInteger(vm, slot)) BL_RAISE(vm, "TypeException", "%s must be an integer.", name);
    return true;
}

bool blCheckStr(BlangVM *vm, int slot, const char *name) {
    if(!blIsString(vm, slot)) BL_RAISE(vm, "TypeException", "%s must be a String.", name);
    return true;
}

bool blCheckList(BlangVM *vm, int slot, const char *name) {
    if(!blIsList(vm, slot)) BL_RAISE(vm, "TypeException", "%s must be a List.", name);
    return true;
}

bool blCheckTuple(BlangVM *vm, int slot, const char *name) {
    if(!blIsTuple(vm, slot)) BL_RAISE(vm, "TypeException", "%s must be a Tuple.", name);
    return true;
}

bool blCheckBool(BlangVM *vm, int slot, const char *name) {
    if(!blIsBoolean(vm, slot)) BL_RAISE(vm, "TypeException", "%s must be a String.", name);
    return true;
}

bool blCheckInstance(BlangVM *vm, int slot, const char *name) {
    if(!blIsInstance(vm, slot)) BL_RAISE(vm, "TypeException", "%s must be an instance.", name);
    return true;
}

bool blCheckHandle(BlangVM *vm, int slot, const char *name) {
    if(!blIsHandle(vm, slot)) BL_RAISE(vm, "TypeException", "%s must be an Handle.", name);
    return true;
}

size_t blCheckIndex(BlangVM *vm, int slot, size_t max, const char *name) {
    if(!blCheckInt(vm, slot, name)) return SIZE_MAX;
    double i = blGetNumber(vm, slot);
    return checkIndex(vm, i, max, name);
}

void blPrintStackTrace(BlangVM *vm) {
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