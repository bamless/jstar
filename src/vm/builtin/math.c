#include "math.h"

#include <stdlib.h>
#include <math.h>

NATIVE(bl_random) {
    blPushNumber(vm, (double) rand() / ((unsigned) RAND_MAX + 1));
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
    blPushNull(vm);
    return true;
}