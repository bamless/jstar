#ifndef INT_HASH_TABLE_H
#define INT_HASH_TABLE_H

#include "hash_table.h"
#include "jstar.h"

DECLARE_HASH_TABLE(Int, int)

void reachIntHashTable(JStarVM* vm, const IntHashTable* t);

#endif
