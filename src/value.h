#ifndef VALUE_H
#define VALUE_H

#include <stdlib.h>
#include <stdint.h>

typedef uint64_t Value;

#define SIGN_BIT ((uint64_t) 1 << 63)
#define SNAN 0x7FF0000000000000

#define NULL_TAG  1
#define TRUE_TAG  2
#define FALSE_TAG 3

#define IS_OBJ(val)    (((val) & (SNAN | SIGN_BIT)) == (SNAN | SIGN_BIT))
#define IS_BOOL(val)   ((val) == TRUE_VAL || (val) == FALSE_VAL)
#define IS_NUM(val)    (((val) & SNAN) != SNAN)
#define IS_NULL(val)   ((val) == NULL_VAL)

#define AS_BOOL(value) ((value) == TRUE_VAL)
#define AS_NUM(value)  valueToNum(value)
#define AS_OBJ(value)  ((Obj*)(uintptr_t)((value) & ~(SIGN_BIT | SNAN)))

#define NUM_VAL(num)   numToValue(num)
#define BOOL_VAL(b)    ((b) ? TRUE_VAL : FALSE_VAL)
#define OBJ_VAL(obj)   ((Value)(SIGN_BIT | SNAN | (uint64_t)(uintptr_t)(obj)))
#define TRUE_VAL       ((Value)(uint64_t) (SNAN | TRUE_TAG))
#define FALSE_VAL      ((Value)(uint64_t) (SNAN | FALSE_TAG))
#define NULL_VAL       ((Value)(uint64_t) (SNAN | NULL_TAG))

typedef union {
	uint64_t raw64;
	double num;
} DoubleConv;

static inline Value numToValue(double num) {
	DoubleConv c;
	c.num = num;
	return c.raw64;
}

static inline double valueToNum(Value val) {
	DoubleConv c;
	c.raw64 = val;
	return c.num;
}

#define VAL_ARR_DEF_SZ 16
#define VAL_ARR_GROW_FAC 2

typedef struct ValueArray {
	size_t size, count;
	Value *arr;
} ValueArray;

void initValueArray(ValueArray *a);
void freeValueArray(ValueArray *a);
size_t valueArrayAppend(ValueArray *a, Value v);

#endif
