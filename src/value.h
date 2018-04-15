#ifndef VALUE_H
#define VALUE_H

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

typedef struct Obj Obj;

#ifdef NAN_TAGGING

typedef uint64_t Value;

#define SIGN_BIT ((uint64_t) 1 << 63)
#define QNAN ((uint64_t) 0x7FFC000000000000)

#define NULL_TAG  1
#define FALSE_TAG 2
#define TRUE_TAG  3

#define IS_OBJ(val)    (((val) & (QNAN | SIGN_BIT)) == (QNAN | SIGN_BIT))
#define IS_BOOL(val)   (((val) & (QNAN | FALSE_TAG)) == (QNAN | FALSE_TAG))
#define IS_NUM(val)    (((val) & QNAN) != QNAN)
#define IS_NULL(val)   ((val) == NULL_VAL)

#define AS_BOOL(value) ((value) == TRUE_VAL)
#define AS_NUM(value)  valueToNum(value)
#define AS_OBJ(value)  ((Obj*)(uintptr_t)((value) & ~(SIGN_BIT | QNAN)))

#define NUM_VAL(num)   numToValue(num)
#define BOOL_VAL(b)    ((b) ? TRUE_VAL : FALSE_VAL)
#define OBJ_VAL(obj)   ((Value)(SIGN_BIT | QNAN | (uint64_t)(uintptr_t)(obj)))
#define TRUE_VAL       ((Value)(uint64_t) (QNAN | TRUE_TAG))
#define FALSE_VAL      ((Value)(uint64_t) (QNAN | FALSE_TAG))
#define NULL_VAL       ((Value)(uint64_t) (QNAN | NULL_TAG))

static inline Value numToValue(double num) {
	union {
		uint64_t raw;
		double   num;
	} c = {.num = num};
	return c.raw;
}

static inline double valueToNum(Value val) {
	union {
		uint64_t raw;
		double   num;
	} c = {.raw = val};
	return c.num;
}

static inline bool valueEquals(Value v1, Value v2) {
	return v1 == v2;
}

#else

typedef struct Obj Obj;

typedef enum {
	VAL_NUM, VAL_BOOL, VAL_OBJ, VAL_NULL
} ValueType;

typedef struct {
	ValueType type;
	union {
		bool boolean;
		double num;
		Obj *obj;
	};
} Value;

#define IS_OBJ(val)    ((val).type == VAL_OBJ)
#define IS_BOOL(val)   ((val).type == VAL_BOOL)
#define IS_NUM(val)    ((val).type == VAL_NUM)
#define IS_NULL(val)   ((val).type == VAL_NULL)

#define AS_BOOL(value) ((value).boolean)
#define AS_NUM(value)  ((value).num)
#define AS_OBJ(value)  ((value).obj)

#define NUM_VAL(n)     ((Value) {VAL_NUM,  {.num = n}})
#define BOOL_VAL(b)    ((Value) {VAL_BOOL, {.boolean = b}})
#define OBJ_VAL(val)   ((Value) {VAL_OBJ,  {.obj = (Obj*) val}})
#define TRUE_VAL       ((Value) {VAL_BOOL, {.boolean = true}})
#define FALSE_VAL      ((Value) {VAL_BOOL, {.boolean = false}})
#define NULL_VAL       ((Value) {VAL_NULL, {.num = 0}})

static inline bool valueEquals(Value v1, Value v2) {
	if(v1.type != v2.type) return false;

	switch(v1.type) {
	case VAL_NUM:
		return v1.num == v2.num;
	case VAL_BOOL:
		return v1.boolean == v2.boolean;
	case VAL_OBJ:
		return v1.obj == v2.obj;
	case VAL_NULL:
		return true;
	}

	return false;
}

#endif

//-- Value array --

#define VAL_ARR_DEF_SZ   8
#define VAL_ARR_GROW_FAC 2

typedef struct ValueArray {
	size_t size, count;
	Value *arr;
} ValueArray;

void initValueArray(ValueArray *a);
void freeValueArray(ValueArray *a);
size_t valueArrayAppend(ValueArray *a, Value v);

void printValue(Value val);

#endif
