#ifndef INT_HASH_TABLE_H
#define INT_HASH_TABLE_H

#include "hashtable.h"
#include "jstar.h"
#include "object_types.h" // IWYU pragma: keep

DECLARE_HASH_TABLE(Int, int)

void reachIntHashTable(JStarVM* vm, const IntHashTable* t);

#endif
