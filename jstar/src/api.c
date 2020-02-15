#include "jstar.h"
#include "import.h"
#include "memory.h"
#include "util.h"
#include "vm.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

/**
 * The bulk of the API (jstar.h) implementation.
 * 
 * The VM entry point functions and others that need to manipulate the VM internals are implemented 
 * at the end of vm.c
 * 
 * The JStarBuffer functions are implemented at the end of object.c
 */

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

void jsrPushTable(JStarVM *vm) {
    validateStack(vm);
    push(vm, OBJ_VAL(newTable(vm)));
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

bool jsrGetGlobal(JStarVM *vm, const char *mname, const char *name) {
    assert(vm->module != NULL || mname != NULL, "no module in top-level getGlobal");

    ObjModule *module;
    if(mname != NULL)
        module = getModule(vm, copyString(vm, mname, strlen(mname), true));
    else
        module = vm->module;

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

size_t jsrCheckIndex(JStarVM *vm, int slot, size_t max, const char *name) {
    if(!jsrCheckInt(vm, slot, name)) return SIZE_MAX;
    double i = jsrGetNumber(vm, slot);
    return checkIndex(vm, i, max, name);
}

void jsrPrintStacktrace(JStarVM *vm, int slot) {
    Value exc = vm->apiStack[apiStackIndex(vm, slot)];
    assert(isInstance(vm, exc, vm->excClass), "Top of stack isn't an exception");
    push(vm, exc);
    jsrCallMethod(vm, "printStacktrace", 0);
    jsrPop(vm);
}