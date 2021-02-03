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
    a->size = a->size == 0 ? VAL_ARR_DEF_SZ : a->size * VAL_ARR_GROW_FAC;
    a->arr = realloc(a->arr, a->size * sizeof(Value));
}

static bool shouldGrow(const ValueArray* a) {
    return a->count + 1 > a->size;
}

static void ensureCapacity(ValueArray* a) {
    if(shouldGrow(a)) grow(a);
}

int valueArrayAppend(ValueArray* a, Value v) {
    ensureCapacity(a);
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

extern inline bool valueIsInt(Value v);
extern inline bool valueEquals(Value v1, Value v2);
extern inline bool valueToBool(Value v);