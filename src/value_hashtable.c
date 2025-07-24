#include "value_hashtable.h"

#include <stddef.h>

#include "gc.h"
#include "hashtable.h"
#include "object.h"  // IWYU pragma: keep
#include "value.h"
#include "vm.h"  // IWYU pragma: keep

#define TOMB_MARKER    NULL_VAL
#define INVALID_VAL    NULL_VAL
#define IS_INVALID_VAL IS_NULL

DEFINE_HASH_TABLE(Value, Value, TOMB_MARKER, INVALID_VAL, IS_INVALID_VAL, 2, 8)

void reachValueHashTable(JStarVM* vm, const ValueHashTable* t) {
    if(t->entries == NULL) return;
    for(size_t i = 0; i <= t->sizeMask; i++) {
        ValueEntry* e = &t->entries[i];
        reachObject(vm, (Obj*)e->key);
        reachValue(vm, e->value);
    }
}

void sweepStrings(ValueHashTable* t) {
    if(t->entries == NULL) return;
    for(size_t i = 0; i <= t->sizeMask; i++) {
        ValueEntry* e = &t->entries[i];
        if(e->key && !e->key->base.reached) {
            *e = (ValueEntry){NULL, TOMB_MARKER};
        }
    }
}
