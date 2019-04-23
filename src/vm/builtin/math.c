#include "math.h"

#include <stdlib.h>

NATIVE(bl_random) {
    blPushNumber(vm, (double) rand() / RAND_MAX);
    return true;
}

NATIVE(bl_seed) {
    if(!blCheckInt(vm, 1, "s")) return false;
    srand(blGetNumber(vm, 1));
    blPushNull(vm);
    return true;
}