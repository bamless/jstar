#include "math.h"

#include <math.h>
#include <stdbool.h>
#include <stdlib.h>
#include <time.h>

#include "object.h"
#include "value.h"
#include "vm.h"

#define JSR_PI 3.14159265358979323846
#define JSR_E  2.71828182845904523536

#define STDLIB_MATH_FUN_X(fun)                        \
    JSR_NATIVE(jsr_##fun) {                           \
        if(!jsrCheckNumber(vm, 1, "x")) return false; \
        jsrPushNumber(vm, fun(jsrGetNumber(vm, 1)));  \
        return true;                                  \
    }

#define STDLIB_MATH_FUN_XY(fun)                                                      \
    JSR_NATIVE(jsr_##fun) {                                                          \
        if(!jsrCheckNumber(vm, 1, "x") || !jsrCheckNumber(vm, 2, "y")) return false; \
        jsrPushNumber(vm, fun(jsrGetNumber(vm, 1), jsrGetNumber(vm, 2)));            \
        return true;                                                                 \
    }

static double deg(double x) {
    return x * (180. / JSR_PI);
}

static double rad(double x) {
    return x * JSR_PI / 180.;
}

// The MSVC stdlib.h header file seem to define `min` and `max` no matter which compilation options
// or define I try to use, so we need to undefine them manually.
// Nice work Microsoft for distributing non-standard compliant header files with your compiler...
#ifdef max
    #undef max
#endif
#ifdef min
    #undef min
#endif

#define max(a, b) ((a) > (b) ? (a) : (b))
#define min(a, b) ((a) < (b) ? (a) : (b))

JSR_NATIVE(jsr_abs) {
    JSR_CHECK(Number, 1, "x");
    jsrPushNumber(vm, fabs(jsrGetNumber(vm, 1)));
    return true;
}

STDLIB_MATH_FUN_X(acos)
STDLIB_MATH_FUN_X(asin)
STDLIB_MATH_FUN_X(atan)

JSR_NATIVE(jsr_atan2) {
    JSR_CHECK(Number, 1, "y");
    JSR_CHECK(Number, 2, "x");
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
    JSR_CHECK(Number, 1, "x");
    double m;
    int e;
    m = frexp(jsrGetNumber(vm, 1), &e);
    ObjTuple* ret = newTuple(vm, 2);
    ret->arr[0] = NUM_VAL(m);
    ret->arr[1] = NUM_VAL(e);
    push(vm, OBJ_VAL(ret));
    return true;
}

JSR_NATIVE(jsr_ldexp) {
    JSR_CHECK(Number, 1, "x");
    JSR_CHECK(Int, 2, "exp");
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
    JSR_CHECK(Number, 1, "x");
    double integer, frac;
    integer = modf(jsrGetNumber(vm, 1), &frac);
    ObjTuple* ret = newTuple(vm, 2);
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
    JSR_CHECK(Int, 1, "s");
    srand(jsrGetNumber(vm, 1));
    jsrPushNull(vm);
    return true;
}

JSR_NATIVE(jsr_math_init) {
    // Init constants
    jsrPushNumber(vm, HUGE_VAL);
    jsrSetGlobal(vm, NULL, "huge");
    jsrPushNumber(vm, NAN);
    jsrSetGlobal(vm, NULL, "nan");
    jsrPushNumber(vm, JSR_PI);
    jsrSetGlobal(vm, NULL, "pi");
    jsrPushNumber(vm, JSR_E);
    jsrSetGlobal(vm, NULL, "e");
    jsrPushNull(vm);
    // Init rand seed
    srand(time(NULL));
    return true;
}