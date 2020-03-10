#include "value.h"

#include <float.h>
#include <stdio.h>
#include <stdlib.h>

#include "object.h"

void initValueArray(ValueArray *a) {
    a->size = 0;
    a->count = 0;
    a->arr = NULL;
}

void freeValueArray(ValueArray *a) {
    a->size = 0;
    a->count = 0;
    free(a->arr);
}

static void grow(ValueArray *a) {
    a->size = a->size == 0 ? VAL_ARR_DEF_SZ : a->size * VAL_ARR_GROW_FAC;
    a->arr = realloc(a->arr, a->size * sizeof(Value));
}

int valueArrayAppend(ValueArray *a, Value v) {
    if(a->count + 1 > a->size) grow(a);

    a->arr[a->count] = v;
    return a->count++;
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
