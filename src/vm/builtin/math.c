#include "math.h"
#include "memory.h"
#include "object.h"
#include "vm.h"

#include <math.h>
#include <stdlib.h>

#define BL_PI 3.14159265358979323846
#define BL_E 2.71828182845904523536

#define STDLIB_MATH_FUN_X(fun)                     \
    JSR_NATIVE(jsr_##fun) {                             \
        if(!jsrCheckNum(vm, 1, "x")) return false;  \
        jsrPushNumber(vm, fun(jsrGetNumber(vm, 1))); \
        return true;                               \
    }

#define STDLIB_MATH_FUN_XY(fun)                                              \
    JSR_NATIVE(jsr_##fun) {                                                       \
        if(!jsrCheckNum(vm, 1, "x") || !jsrCheckNum(vm, 2, "y")) return false; \
        jsrPushNumber(vm, fun(jsrGetNumber(vm, 1), jsrGetNumber(vm, 2)));       \
        return true;                                                         \
    }

static double deg(double x) {
    return x * (180. / BL_PI);
}

static double rad(double x) {
    return x * BL_PI / 180.;
}

#define max(a, b) ((a) > (b) ? (a) : (b))
#define min(a, b) ((a) < (b) ? (a) : (b))

JSR_NATIVE(jsr_abs) {
    if(!jsrCheckNum(vm, 1, "x")) return false;
    jsrPushNumber(vm, fabs(jsrGetNumber(vm, 1)));
    return true;
}

STDLIB_MATH_FUN_X(acos)
STDLIB_MATH_FUN_X(asin)
STDLIB_MATH_FUN_X(atan)

JSR_NATIVE(jsr_atan2) {
    if(!jsrCheckNum(vm, 1, "y") || !jsrCheckNum(vm, 2, "x")) {
        return false;
    }
    jsrPushNumber(vm, atan2(jsrGetNumber(vm, 1), jsrGetNumber(vm, 2)));
    return true;
}

STDLIB_MATH_FUN_X(ceil)
STDLIB_MATH_FUN_X(cos)
STDLIB_MATH_FUN_X(cosh)
STDLIB_MATH_FUN_X(deg)
STDLIB_MATH_FUN_X(exp)
STDLIB_MATH_FUN_X(floor)

JSR_NATIVE(jsr_frexp) {
    if(!jsrCheckNum(vm, 1, "x")) return false;
    double m;
    int e;
    m = frexp(jsrGetNumber(vm, 1), &e);
    ObjTuple *ret = newTuple(vm, 2);
    ret->arr[0] = NUM_VAL(m);
    ret->arr[1] = NUM_VAL(e);
    push(vm, OBJ_VAL(ret));
    return true;
}

JSR_NATIVE(jsr_ldexp) {
    if(!jsrCheckNum(vm, 1, "x") || !jsrCheckInt(vm, 2, "exp")) {
        return false;
    }
    jsrPushNumber(vm, ldexp(jsrGetNumber(vm, 1), jsrGetNumber(vm, 2)));
    return true;
}

STDLIB_MATH_FUN_X(log)
STDLIB_MATH_FUN_X(log10)
STDLIB_MATH_FUN_XY(max)
STDLIB_MATH_FUN_XY(min)
STDLIB_MATH_FUN_X(rad)
STDLIB_MATH_FUN_X(sin)
STDLIB_MATH_FUN_X(sinh)
STDLIB_MATH_FUN_X(sqrt)
STDLIB_MATH_FUN_X(tan)
STDLIB_MATH_FUN_X(tanh)

JSR_NATIVE(jsr_modf) {
    if(!jsrCheckNum(vm, 1, "x")) return false;
    double integer, frac;
    integer = modf(jsrGetNumber(vm, 1), &frac);
    ObjTuple *ret = newTuple(vm, 2);
    ret->arr[0] = NUM_VAL(integer);
    ret->arr[1] = NUM_VAL(frac);
    push(vm, OBJ_VAL(ret));
    return true;
}

JSR_NATIVE(jsr_random) {
    jsrPushNumber(vm, (double)rand() / ((unsigned)RAND_MAX + 1));
    return true;
}

JSR_NATIVE(jsr_seed) {
    if(!jsrCheckInt(vm, 1, "s")) return false;
    srand(jsrGetNumber(vm, 1));
    jsrPushNull(vm);
    return true;
}

JSR_NATIVE(jsr_math_init) {
    jsrPushNumber(vm, HUGE_VAL);
    jsrSetGlobal(vm, NULL, "huge");
    jsrPushNumber(vm, NAN);
    jsrSetGlobal(vm, NULL, "nan");
    jsrPushNumber(vm, BL_PI);
    jsrSetGlobal(vm, NULL, "pi");
    jsrPushNumber(vm, BL_E);
    jsrSetGlobal(vm, NULL, "e");
    jsrPushNull(vm);
    return true;
}