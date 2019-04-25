#include "debug.h"
#include "chunk.h"
#include "vm.h"

#include "debug/disassemble.h"

#include <stdio.h>

NATIVE(bl_printStack) {
    for(Value *v = vm->stack; v < vm->sp; v++) {
        printf("[");
        printValue(*v);
        printf("]");
    }
    printf("$\n");

    blPushNull(vm);
    return true;
}

NATIVE(bl_dis) {
    if(!IS_OBJ(vm->apiStack[1]) || !(IS_CLOSURE(vm->apiStack[1]) || IS_NATIVE(vm->apiStack[1]) ||
                                     IS_BOUND_METHOD(vm->apiStack[1]))) {
        BL_RAISE(vm, "InvalidArgException", "Argument to dis must be a function object.");
    }

    Value func = vm->apiStack[1];
    if(IS_BOUND_METHOD(func)) {
        func = OBJ_VAL(AS_BOUND_METHOD(func)->method);
    }

    if(!IS_NATIVE(func)) {
        Chunk *c = &AS_CLOSURE(func)->fn->chunk;
        disassembleChunk(c);
    } else {
        printf("Native implementation\n");
    }

    blPushNull(vm);
    return true;
}
