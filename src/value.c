#include "value.h"

void initValueArray(ValueArray *a) {
	a->size = VAL_ARR_DEF_SZ;
	a->count = 0;
	a->arr = malloc(sizeof(Value) * VAL_ARR_DEF_SZ);
}

void freeValueArray(ValueArray *a) {
	a->size = 0;
	a->count = 0;
	free(a->arr);
}

static void grow(ValueArray *a) {
	a->size *= VAL_ARR_GROW_FAC;
	a->arr = realloc(a->arr, a->size * sizeof(Value));
}

void valueArrayAppend(ValueArray *a, Value v) {
	if(a->count + 1 > a->size)
		grow(a);

	a->arr[a->count++] = v;
}
