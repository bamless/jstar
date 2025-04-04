#ifndef SYMBOL_H
#define SYMBOL_H

#include <stdint.h>

#include "object_types.h"
#include "value.h"

// The type of a cached symbol.
// It can be:
//  - `SYMBOL_METHOD`, for caching method's lookups. When in this state the `as.method` field is
//    valid and contains the resolved method's value.
//  - `SYMBOL_BOUND_METHOD`, for caching bound method's lookups. When in this state the `as.method`
//    field is valid and contains the resolved method's value. Used primarily to distinguish between
//    a regular method lookup and a bound method lookup, so we can instantiate a brand new bound
//    method when hitting the cache.
//  - `SYMBOL_FIELD`, for caching field's lookups. When in this state the `as.offset` field is valid
//    and contains the resolved field's offset inside the object.
//  - `SYMBOL_GLOBAL`, for caching global variable's lookups. When in this state the `as.offset`
//    field is valid and contains the resolved global variable's offset inside the module.
typedef enum {
    SYMBOL_METHOD,
    SYMBOL_BOUND_METHOD,
    SYMBOL_FIELD,
    SYMBOL_GLOBAL,
} SymbolType;

// Symbol cache used to speed up method/field/global var lookups during VM evaluation.
// It caches the result of a name resolution, so we don't have to look it up again.
typedef struct SymbolCache {
    SymbolType type;  // The type of the cached symbol
    Obj* key;         // The key of the cached symbol. Used to invalidate the cache
    union {
        Value method;   // The cached method
        size_t offset;  // The offset of the cached field or global variable inside of its object
    } as;
} SymbolCache;

// A symbol pointing to a constant in the constant pool.
// Includes a cache to speed up symbol lookups during VM evaluation.
typedef struct Symbol {
    uint16_t constant;  // The index of the constant in the constant pool
    SymbolCache cache;  // The cache for the symbol's value
} Symbol;

#endif
