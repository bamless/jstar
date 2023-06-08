#include "std.h"

#include <math.h>
#include <limits.h>

#include "gc.h"
#include "import.h"
#include "object.h"
#include "parse/ast.h"
#include "parse/parser.h"
#include "vm.h"

JSR_NATIVE(jsr_int) {
    if(jsrIsNumber(vm, 1)) {
        jsrPushNumber(vm, trunc(jsrGetNumber(vm, 1)));
        return true;
    }
    if(jsrIsString(vm, 1)) {
        char* end = NULL;
        const char* nstr = jsrGetString(vm, 1);
        long long n = strtoll(nstr, &end, 10);

        if((n == 0 && end == nstr) || *end != '\0') {
            JSR_RAISE(vm, "InvalidArgException", "'%s'.", nstr);
        }
        if(n == LLONG_MAX) {
            JSR_RAISE(vm, "InvalidArgException", "Overflow: '%s'.", nstr);
        }
        if(n == LLONG_MIN) {
            JSR_RAISE(vm, "InvalidArgException", "Underflow: '%s'.", nstr);
        }

        jsrPushNumber(vm, n);
        return true;
    }
    JSR_RAISE(vm, "TypeException", "Argument must be a number or a string.");
}

JSR_NATIVE(jsr_char) {
    JSR_CHECK(String, 1, "c");
    const char* str = jsrGetString(vm, 1);
    if(jsrGetStringSz(vm, 1) != 1) {
        JSR_RAISE(vm, "InvalidArgException", "c must be a String of length 1");
    }
    int c = str[0];
    jsrPushNumber(vm, (double)c);
    return true;
}

JSR_NATIVE(jsr_garbageCollect) {
    garbageCollect(vm);
    jsrPushNull(vm);
    return true;
}

JSR_NATIVE(jsr_ascii) {
    JSR_CHECK(Int, 1, "num");
    char c = jsrGetNumber(vm, 1);
    jsrPushStringSz(vm, &c, 1);
    return true;
}

JSR_NATIVE(jsr_print) {
    jsrPushValue(vm, 1);
    if(jsrCallMethod(vm, "__string__", 0) != JSR_SUCCESS) return false;
    if(!jsrIsString(vm, -1)) {
        JSR_RAISE(vm, "TypeException", "s.__string__() didn't return a String");
    }

    fwrite(jsrGetString(vm, -1), 1, jsrGetStringSz(vm, -1), stdout);
    jsrPop(vm);

    JSR_FOREACH(2, {
        if(jsrCallMethod(vm, "__string__", 0) != JSR_SUCCESS) return false;
        if(!jsrIsString(vm, -1)) {
            JSR_RAISE(vm, "TypeException", "__string__() didn't return a String");
        }
        
        printf(" ");
        fwrite(jsrGetString(vm, -1), 1, jsrGetStringSz(vm, -1), stdout);
        jsrPop(vm);
    },);

    printf("\n");

    jsrPushNull(vm);
    return true;
}

static void parseError(const char* file, int line, const char* error, void* udata) {
    JStarVM* vm = udata;
    vm->errorCallback(vm, JSR_SYNTAX_ERR, file, line, error);
}

JSR_NATIVE(jsr_eval) {
    JSR_CHECK(String, 1, "source");

    if(vm->frameCount < 1) {
        JSR_RAISE(vm, "Exception", "eval() can only be called by another function");
    }

    JStarStmt* program = jsrParse("<eval>", jsrGetString(vm, 1), parseError, vm);
    if(program == NULL) {
        JSR_RAISE(vm, "SyntaxException", "Syntax error");
    }

    Prototype* proto = getPrototype(vm->frames[vm->frameCount - 2].fn);
    ObjFunction* fn = compileModule(vm, "<eval>", proto->module->name, program);
    jsrStmtFree(program);

    if(fn == NULL) {
        JSR_RAISE(vm, "SyntaxException", "Syntax error");
    }

    push(vm, OBJ_VAL(fn));
    ObjClosure* closure = newClosure(vm, fn);
    pop(vm);

    push(vm, OBJ_VAL(closure));
    if(jsrCall(vm, 0) != JSR_SUCCESS) return false;
    pop(vm);

    jsrPushNull(vm);
    return true;
}

JSR_NATIVE(jsr_type) {
    push(vm, OBJ_VAL(getClass(vm, peek(vm))));
    return true;
}

