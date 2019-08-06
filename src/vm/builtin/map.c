#include "map.h"
#include "memory.h"
#include "object.h"
#include "vm.h"

// TODO: consider implementing Map as a built-in object for performance
// class Map
JSR_NATIVE(jsr_Map_getEntry) {
    jsrPushValue(vm, 1);
    if(jsrCallMethod(vm, "__hash__", 0) != VM_EVAL_SUCCESS) return false;

    size_t i = AS_NUM(pop(vm));

    if(!jsrGetField(vm, 0, "_entries")) return false;

    ObjList *lst = AS_LIST(pop(vm));
    size_t count = lst->count;
    Value *entries = lst->arr;

    size_t hash = i & (count - 1);

    Value buck = entries[hash];

    while(!IS_NULL(buck)) {
        push(vm, buck);

        jsrGetField(vm, -1, "key");
        jsrPushValue(vm, 1);

        if(!jsrEquals(vm)) return false;

        if(jsrIsBoolean(vm, -1) && AS_BOOL(peek(vm))) {
            pop(vm);
            return true;
        }
        pop(vm);

        jsrGetField(vm, -1, "next");
        buck = pop(vm);

        pop(vm);
    }

    jsrPushNull(vm);
    return true;
}

JSR_NATIVE(jsr_Map_addEntry) {
    if(!jsrGetField(vm, -1, "key")) return false;
    if(jsrCallMethod(vm, "__hash__", 0) != VM_EVAL_SUCCESS) return false;

    size_t i = AS_NUM(pop(vm));

    if(!jsrGetField(vm, 0, "_entries")) return false;

    ObjList *lst = AS_LIST(pop(vm));
    size_t count = lst->count;
    Value *entries = lst->arr;

    size_t hash = i & (count - 1);

    push(vm, entries[hash]);
    jsrSetField(vm, -2, "next");

    entries[hash] = vm->apiStack[1];
    return true;
}

JSR_NATIVE(jsr_Map_grow) {
    if(!jsrGetField(vm, 0, "_entries")) return false;

    ObjList *lst = AS_LIST(pop(vm));
    Value *oldEntries = lst->arr;
    size_t size = lst->count;

    size_t newSize = size * 2;

    jsrPushNumber(vm, newSize);
    jsrSetField(vm, 0, "_size");
    pop(vm);

    lst = newList(vm, newSize);
    Value *entries = lst->arr;
    lst->count = newSize;

    for(size_t i = 0; i < newSize; i++) {
        entries[i] = NULL_VAL;
    }

    push(vm, OBJ_VAL(lst));
    jsrSetField(vm, 0, "_entries");
    pop(vm);

    for(size_t i = 0; i < size; i++) {
        Value buck = oldEntries[i];

        while(!IS_NULL(buck)) {
            push(vm, buck);
            jsrGetField(vm, -1, "next");
            Value next = pop(vm);
            pop(vm);

            jsrPushValue(vm, 0);
            push(vm, buck);
            if(jsrCallMethod(vm, "__addEntry", 1) != VM_EVAL_SUCCESS) return false;
            pop(vm);

            buck = next;
        }
    }

    jsrPushNull(vm);
    return true;
}
// end