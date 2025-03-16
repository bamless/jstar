#include "value.h"

#include <float.h>
#include <stdio.h>
#include <stdlib.h>

#include "object.h"
#include "util.h"

void initValueArray(ValueArray* a) {
    *a = (ValueArray){0};
}

void freeValueArray(ValueArray* a) {
    free(a->arr);
}

int valueArrayAppend(ValueArray* a, Value v) {
    ARRAY_APPEND(a, size, capacity, arr, v);
    return a->size - 1;
}

void printValue(Value val) {
    if(IS_OBJ(val)) {
        printObj(AS_OBJ(val));
    } else if(IS_BOOL(val)) {
        printf(AS_BOOL(val) ? "true" : "false");
    } else if(IS_NUM(val)) {
        printf("%.*g", DBL_DIG, AS_NUM(val));
    } else if(IS_HANDLE(val)) {
        printf("<handle:%p>", AS_HANDLE(val));
    } else if(IS_NULL(val)) {
        printf("null");
    }
}

extern inline bool valueIsInt(Value v);
extern inline bool valueEquals(Value v1, Value v2);
extern inline bool valueToBool(Value v);
