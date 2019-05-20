#include "math.h"
#include "memory.h"
#include "object.h"
#include "vm.h"

#include <math.h>
#include <stdlib.h>

#define BL_PI 3.14159265358979323846
#define BL_E 2.71828182845904523536

#define STDLIB_MATH_FUN_X(fun)                     \
    NATIVE(bl_##fun) {                             \
        if(!blCheckNum(vm, 1, "x")) return false;  \
        blPushNumber(vm, fun(blGetNumber(vm, 1))); \
        return true;                               \
    }

#define STDLIB_MATH_FUN_XY(fun)                                              \
    NATIVE(bl_##fun) {                                                       \
        if(!blCheckNum(vm, 1, "x") || !blCheckNum(vm, 2, "y")) return false; \
        blPushNumber(vm, fun(blGetNumber(vm, 1), blGetNumber(vm, 2)));       \
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

NATIVE(bl_abs) {
    if(!blCheckNum(vm, 1, "x")) return false;
    blPushNumber(vm, fabs(blGetNumber(vm, 1)));
    return true;
}

STDLIB_MATH_FUN_X(acos)
STDLIB_MATH_FUN_X(asin)
STDLIB_MATH_FUN_X(atan)

NATIVE(bl_atan2) {
    if(!blCheckNum(vm, 1, "y") || !blCheckNum(vm, 2, "x")) {
        return false;
    }
    blPushNumber(vm, atan2(blGetNumber(vm, 1), blGetNumber(vm, 2)));
    return true;
}

STDLIB_MATH_FUN_X(ceil)
STDLIB_MATH_FUN_X(cos)
STDLIB_MATH_FUN_X(cosh)
STDLIB_MATH_FUN_X(deg)
STDLIB_MATH_FUN_X(exp)
STDLIB_MATH_FUN_X(floor)

NATIVE(bl_frexp) {
    if(!blCheckNum(vm, 1, "x")) return false;
    double m;
    int e;
    m = frexp(blGetNumber(vm, 1), &e);
    ObjTuple *ret = newTuple(vm, 2);
    ret->arr[0] = NUM_VAL(m);
    ret->arr[1] = NUM_VAL(e);
    push(vm, OBJ_VAL(ret));
    return true;
}

NATIVE(bl_ldexp) {
    if(!blCheckNum(vm, 1, "x") || !blCheckInt(vm, 2, "exp")) {
        return false;
    }
    blPushNumber(vm, ldexp(blGetNumber(vm, 1), blGetNumber(vm, 2)));
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

NATIVE(bl_modf) {
    if(!blCheckNum(vm, 1, "x")) return false;
    double integer, frac;
    integer = modf(blGetNumber(vm, 1), &frac);
    ObjTuple *ret = newTuple(vm, 2);
    ret->arr[0] = NUM_VAL(integer);
    ret->arr[1] = NUM_VAL(frac);
    push(vm, OBJ_VAL(ret));
    return true;
}

NATIVE(bl_random) {
    blPushNumber(vm, (double)rand() / ((unsigned)RAND_MAX + 1));
    return true;
}

NATIVE(bl_seed) {
    if(!blCheckInt(vm, 1, "s")) return false;
    srand(blGetNumber(vm, 1));
    blPushNull(vm);
    return true;
}

NATIVE(bl_math_init) {
    blPushNumber(vm, HUGE_VAL);
    blSetGlobal(vm, NULL, "huge");
    blPushNumber(vm, NAN);
    blSetGlobal(vm, NULL, "nan");
    blPushNumber(vm, BL_PI);
    blSetGlobal(vm, NULL, "pi");
    blPushNumber(vm, BL_E);
    blSetGlobal(vm, NULL, "e");
    blPushNull(vm);
    return true;
}