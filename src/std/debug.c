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
    Value arg = vm->apiStack[1];
    if(!IS_OBJ(arg) || !(IS_CLOSURE(arg) || IS_NATIVE(arg) || IS_BOUND_METHOD(arg))) {
        JSR_RAISE(vm, "InvalidArgException", "Cannot disassemble a %s",
                  getClass(vm, arg)->name->data);
    }

    if(IS_BOUND_METHOD(arg)) {
        arg = OBJ_VAL(AS_BOUND_METHOD(arg)->method);
    }

    if(IS_NATIVE(arg)) {
        printf("Native implementation\n");
    } else {
        ObjFunction* fn = AS_CLOSURE(arg)->fn;
        disassembleFunction(fn);
    }

    jsrPushNull(vm);
    return true;
}
