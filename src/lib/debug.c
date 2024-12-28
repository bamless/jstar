#include "debug.h"

#include <stdbool.h>
#include <stdio.h>

#include "disassemble.h"
#include "hashtable.h"
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

static bool isDisassemblable(Value v) {
    return (IS_OBJ(v) && (IS_CLOSURE(v) || IS_NATIVE(v) || IS_BOUND_METHOD(v) || IS_CLASS(v)));
}

JSR_NATIVE(jsr_disassemble) {
    Value arg = vm->apiStack[1];
    if(!isDisassemblable(arg)) {
        JSR_RAISE(vm, "InvalidArgException", "Cannot disassemble a %s",
                  getClass(vm, arg)->name->data);
    }

    if(IS_BOUND_METHOD(arg)) {
        arg = OBJ_VAL(AS_BOUND_METHOD(arg)->method);
    } else if(IS_CLASS(arg)) {
        Value ctor;
        if(!hashTableGet(&AS_CLASS(arg)->methods, vm->specialMethods[METH_CTOR], &ctor)) {
            jsrPushNull(vm);
            return true;
        }
        arg = ctor;
    }

    if(IS_NATIVE(arg)) {
        disassembleNative(AS_NATIVE(arg));
    } else {
        disassembleFunction(AS_CLOSURE(arg)->fn);
    }

    jsrPushNull(vm);
    return true;
}
