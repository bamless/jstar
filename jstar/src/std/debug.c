#include "debug.h"

#include <stdbool.h>
#include <stdio.h>

#include "code.h"
#include "disassemble.h"
#include "object.h"
#include "value.h"
#include "vm.h"

JSR_NATIVE(jsr_printStack) {
    for(Value* v = vm->stack; v < vm->sp; v++) {
        printf("[");
        printValue(*v);
        printf("]");
    }
    printf("$\n");
    jsrPushNull(vm);
    return true;
}

JSR_NATIVE(jsr_disassemble) {
    if(!IS_OBJ(vm->apiStack[1]) || !(IS_CLOSURE(vm->apiStack[1]) || IS_NATIVE(vm->apiStack[1]) ||
                                     IS_BOUND_METHOD(vm->apiStack[1]))) {
        JSR_RAISE(vm, "InvalidArgException", "Argument to dis must be a function object.");
    }

    Value func = vm->apiStack[1];
    if(IS_BOUND_METHOD(func)) {
        func = OBJ_VAL(AS_BOUND_METHOD(func)->method);
    }

    if(!IS_NATIVE(func)) {
        Code* c = &AS_CLOSURE(func)->fn->code;
        disassembleCode(c);
    } else {
        printf("Native implementation\n");
    }

    jsrPushNull(vm);
    return true;
}
