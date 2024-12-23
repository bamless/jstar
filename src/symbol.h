#ifndef SYMBOL_H
#define SYMBOL_H

#include <stdint.h>

#include "value.h"

// The type of a cached symbol.
// It can be:
// - A method, for caching method's lookups. Whe in this state the `as.method` field is valid and
//   contains the resolved method's value.
// - A field, for caching field's lookups. When in this state the `as.offset` field is valid and
//   contains the resolved field's offset inside the object.
// - A global variable, for caching global variable's lookups. When in this state the `as.offset`
//   field is valid and contains the resolved global variable's offset inside the module.
// - A bound method, for caching bound method's lookups. When in this state the `as.method` field is
//   valid and contains the resolved bound method's value. Used primarily to distinguish between
//   a regular method lookup and a bound method lookup, so we can instantiate a brand new bound
//   method when hitting the cache.
enum SymbolType { SYMBOL_METHOD, SYMBOL_BOUND_METHOD, SYMBOL_FIELD, SYMBOL_GLOBAL };

// A symbol pointing to a constant in the constant pool.
// Includes a cache for the value of the name resolution.
// It can either be a method, field or global variable.
// Used to implement a caching scheme during VM evaluation.
typedef struct Symbol {
    uint16_t constant;     // The index of the constant in the constant pool
    Obj* key;              // The key of the cached symbol. Used to invalidate the cache
    enum SymbolType type;  // The type of the cached symbol
    union {
        Value method;   // The cached method
        size_t offset;  // The offset of the cached field or global variable inside its object
    } as;
} Symbol;

#endif
