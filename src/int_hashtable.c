#include "int_hashtable.h"

#include <stddef.h>

#include "gc.h"
#include "hashtable.h"
#include "object.h"  // IWYU pragma: keep

#define TOMB_MARKER       -1
#define INVALID_VAL       -2
#define IS_INVALID_VAL(v) ((v) == INVALID_VAL)

DEFINE_HASH_TABLE(Int, int, TOMB_MARKER, INVALID_VAL, IS_INVALID_VAL, 2, 8)

void reachIntHashTable(JStarVM* vm, const IntHashTable* t) {
    if(t->entries == NULL) return;
    for(size_t i = 0; i <= t->sizeMask; i++) {
        IntEntry* e = &t->entries[i];
        reachObject(vm, (Obj*)e->key);
    }
}
