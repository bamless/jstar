#ifndef VALUE_H
#define VALUE_H

#include <stdbool.h>
#include <stdint.h>

#include "jstarconf.h"

typedef struct Obj Obj;
typedef struct ObjString ObjString;

/**
 * Here we define the Value type. This is a C type that can store any type
 * used by the J* VM. This closes the gap between the dinamically typed
 * world of J* and the static one of the C language. Every storage location
 * used in the VM is of type Value (for example the vm stack) as to permit
 * the storage of any J* variable.
 *
 * Note that even tough in J* all values are objects, primitive values
 * such as numbers, booleans and the null singleton are unboxed: they are stored
 * directly in the Value, instead of an Object on the heap. this saves allocations
 * and pointer dereferencing when working with such values.
 *
 * Two implementations of Value are supported:
 *  - NaN tagging technique (explained below). More memory efficient.
 *  - Tagged Union. The classic way to implement such a type. it requires more
 *    than a word of memory to store a single value (tipically 2 words due to padding)
 */

#ifdef JSTAR_NAN_TAGGING

/**
 * NaN Tagging technique. Instead of using a Tagged union (see below) for
 * implementing a type that can hold multiple C types, we use a single 64
 * bit integer and exploit the fact that a NaN IEEE double has 52 + 1
 * unused bits we can utilize.
 *
 * If Value doesn't have the NaN bits set, then it's a valid double. This is
 * convinient because we don't need extra manipulation to extract the double
 * from the value, we only reinterpret the bits.
 *
 * If the NaN bits and the sign bit are set, then the Value is an Object*
 * pointer. We stuff the 64 bit pointer into the 52 bit unused mantissa.
 * This is usally enough since operating systems allocate memory starting at
 * low adresses, thus leaving the most significant bits of an address at zero.
 *
 * If the NaN bits are set and the sign bit isn't, it's either a singleton Value
 * or an handle value.
 * If the two least significant bit of the mantissa aren't both 0, then it is a
 * singleton value. Here we use these two bits to differentiate between null (01),
 * false (10) and true (11).
 * Otherwise it's an Handle (a raw void* C pointer). Similar to the Object* case,
 * we stuff the void* pointer in the remaining bits (this time 50).
 *
 * Using this technique we can store all the needed values used by the J* VM into one
 * 64-bit integer, thus saving the extra space needed by the tag in the union (plus padding
 * bits).
 */

typedef uint64_t Value;

    // clang-format off

#define SIGN ((uint64_t)1 << 63)
#define QNAN ((uint64_t)0x7FFC000000000000)

#define MASK_TAG  3
#define NULL_TAG  1
#define FALSE_TAG 2
#define TRUE_TAG  3

#define GET_TAG(val) ((val) & MASK_TAG)

#define IS_NULL(val)   ((val) == NULL_VAL)
#define IS_NUM(val)    (((val) & (QNAN)) != QNAN)
#define IS_BOOL(val)   (((val) & (FALSE_VAL)) == FALSE_VAL)
#define IS_HANDLE(val) (((val) & (SIGN | TRUE_VAL)) == QNAN)
#define IS_OBJ(val)    (((val) & (QNAN | SIGN)) == (QNAN | SIGN))
#define IS_INT(val)    valueIsInt(val)

#define AS_BOOL(val)   ((val) == TRUE_VAL)
#define AS_HANDLE(val) ((void*)(uintptr_t)(((val) & ~QNAN) >> 2))
#define AS_OBJ(val)    ((Obj*)(uintptr_t)((val) & ~(SIGN | QNAN)))
#define AS_NUM(val)    valueToNum(val)

#define TRUE_VAL      ((Value)(QNAN | TRUE_TAG))
#define FALSE_VAL     ((Value)(QNAN | FALSE_TAG))
#define NULL_VAL      ((Value)(QNAN | NULL_TAG))
#define BOOL_VAL(b)   ((b) ? TRUE_VAL : FALSE_VAL)
#define HANDLE_VAL(h) ((Value)(QNAN | (uint64_t)((uintptr_t)(h) << 2)))
#define OBJ_VAL(obj)  ((Value)(SIGN | QNAN | (uint64_t)(uintptr_t)(obj)))
#define NUM_VAL(num)  numToValue(num)

// clang-format on

inline Value numToValue(double num) {
    union {
        Value val;
        double num;
    } convert = {.num = num};
    return convert.val;
}

inline double valueToNum(Value val) {
    union {
        Value val;
        double num;
    } convert = {.val = val};
    return convert.num;
}

inline bool valueEquals(Value v1, Value v2) {
    return IS_NUM(v1) && IS_NUM(v2) ? AS_NUM(v1) == AS_NUM(v2) : v1 == v2;
}

#else

typedef enum {
    VAL_NUM,
    VAL_BOOL,
    VAL_OBJ,
    VAL_NULL,
    VAL_HANDLE,
} ValueType;

typedef struct {
    ValueType type;
    union {
        bool boolean;
        double num;
        void* handle;
        Obj* obj;
    } as;
} Value;

    // clang-format off

#define IS_HANDLE(val) ((val).type == VAL_HANDLE)
#define IS_OBJ(val)    ((val).type == VAL_OBJ)
#define IS_BOOL(val)   ((val).type == VAL_BOOL)
#define IS_NUM(val)    ((val).type == VAL_NUM)
#define IS_NULL(val)   ((val).type == VAL_NULL)
#define IS_INT(val)    valueIsInt(val)

#define AS_HANDLE(val) ((val).as.handle)
#define AS_BOOL(val)   ((val).as.boolean)
#define AS_NUM(val)    ((val).as.num)
#define AS_OBJ(val)    ((val).as.obj)

#define HANDLE_VAL(h) ((Value){VAL_HANDLE, .as = {.handle = h}})
#define NUM_VAL(n)    ((Value){VAL_NUM, .as = {.num = n}})
#define BOOL_VAL(b)   ((Value){VAL_BOOL, .as = {.boolean = b}})
#define OBJ_VAL(val)  ((Value){VAL_OBJ, .as = {.obj = (Obj*)val}})
#define TRUE_VAL      ((Value){VAL_BOOL, .as = {.boolean = true}})
#define FALSE_VAL     ((Value){VAL_BOOL, .as = {.boolean = false}})
#define NULL_VAL      ((Value){VAL_NULL, .as = {.num = 0}})

// clang-format on

inline bool valueEquals(Value v1, Value v2) {
    if(v1.type != v2.type) return false;

    switch(v1.type) {
    case VAL_NUM:
        return v1.as.num == v2.as.num;
    case VAL_BOOL:
        return v1.as.boolean == v2.as.boolean;
    case VAL_OBJ:
        return v1.as.obj == v2.as.obj;
    case VAL_HANDLE:
        return v1.as.handle == v2.as.handle;
    case VAL_NULL:
        return true;
    }

    return false;
}

#endif

inline bool valueIsInt(Value val) {
    return IS_NUM(val) && (int64_t)AS_NUM(val) == AS_NUM(val);
}

inline bool valueToBool(Value v) {
    if(IS_BOOL(v)) return AS_BOOL(v);
    return !IS_NULL(v);
}

// -----------------------------------------------------------------------------
// VALUE ARRAY
// -----------------------------------------------------------------------------

#define VAL_ARR_DEF_SZ   8
#define VAL_ARR_GROW_FAC 2

typedef struct ValueArray {
    int size, count;
    Value* arr;
} ValueArray;

void initValueArray(ValueArray* a);
void freeValueArray(ValueArray* a);
int valueArrayAppend(ValueArray* a, Value v);

void printValue(Value val);

#endif
