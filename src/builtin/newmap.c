#include "newmap.h"

#include "vm.h"
#include "memory.h"
#include "object.h"

// class Map
    NATIVE(bl_Map_getEntry) {
        blPushValue(vm, 1);
        if(blCallMethod(vm, "__hash__", 0) != VM_EVAL_SUCCSESS) return false;
        
        size_t i = AS_NUM(pop(vm));

        if(!blGetField(vm, 0, "_entries")) return false;

        ObjList *lst = AS_LIST(pop(vm));
        size_t count = lst->count;
        Value *entries = lst->arr;

        size_t hash = i & (count - 1);

        Value buck = entries[hash];

        while(!IS_NULL(buck)) {
            push(vm, buck);

            blGetField(vm, -1, "key");
            blPushValue(vm, 1);
            
            if(blCallMethod(vm, "__eq__", 1) != VM_EVAL_SUCCSESS) return false;
            
            if(blIsBoolean(vm, -1) && AS_BOOL(pop(vm))) {
                return true;
            }
            
            blGetField(vm, -1, "next");
            buck = pop(vm);
            
            pop(vm);
        }

        blPushNull(vm);
        return true;
    }

    NATIVE(bl_Map_addEntry) {
        if(!blGetField(vm, - 1, "key")) return false;
        if(blCallMethod(vm, "__hash__", 0) != VM_EVAL_SUCCSESS) return false;
        
        size_t i = AS_NUM(pop(vm));

        if(!blGetField(vm, 0, "_entries")) return false;

        ObjList *lst = AS_LIST(pop(vm));
        size_t count = lst->count;
        Value *entries = lst->arr;

        size_t hash = i & (count - 1);

        push(vm, entries[hash]);
        blSetField(vm, -2, "next");

        entries[hash] = vm->apiStack[1];
        return true;
    }

    NATIVE(bl_Map_grow) {
        if(!blGetField(vm, 0, "_entries")) return false;

        ObjList *lst = AS_LIST(pop(vm));
        Value *oldEntries = lst->arr;
        size_t size = lst->count;

        size_t newSize = size * 2;

        blPushNumber(vm, newSize);
        blSetField(vm, 0, "_size");
        pop(vm);

        lst = newList(vm, newSize);
        Value *entries = lst->arr;
        lst->count = newSize;

        for(size_t i = 0; i < newSize; i++) {
            entries[i] = NULL_VAL;
        }

        for(size_t i = 0; i < size; i++) {
            Value buck = oldEntries[i];

            while(!IS_NULL(buck)) {
                push(vm, buck);
                blGetField(vm, -1, "next");
                Value next = pop(vm);
                pop(vm);

                blPushValue(vm, 0);
                push(vm, buck);
                if(blCallMethod(vm, "__addEntry", 1) != VM_EVAL_SUCCSESS) return false;
                pop(vm);

                buck = next;
            }
        }

        push(vm, OBJ_VAL(lst));
        blSetField(vm, 0, "_entries");

        blPushNull(vm);
        return true;
    }
// end