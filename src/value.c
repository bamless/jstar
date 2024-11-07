#include "value.h"

#include <float.h>
#include <stdio.h>
#include <stdlib.h>

#include "object.h"

void initValueArray(ValueArray* a) {
    *a = (ValueArray){0};
}

void freeValueArray(ValueArray* a) {
    free(a->arr);
}

static void grow(ValueArray* a) {
    a->capacity = a->capacity ? a->capacity * VAL_ARR_GROW_FAC : VAL_ARR_DEF_SZ;
    a->arr = realloc(a->arr, a->capacity * sizeof(Value));
}

static void ensureCapacity(ValueArray* a) {
    if(a->size + 1 > a->capacity) {
        grow(a);
    }
}

int valueArrayAppend(ValueArray* a, Value v) {
    ensureCapacity(a);
    a->arr[a->size] = v;
    return a->size++;
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
