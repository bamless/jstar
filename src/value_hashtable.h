#ifndef VALUE_HASH_TABLE_H
#define VALUE_HASH_TABLE_H

#include "hashtable.h"
#include "jstar.h"
#include "value.h"

DECLARE_HASH_TABLE(Value, Value)

void reachValueHashTable(JStarVM* vm, const ValueHashTable* t);
void sweepStrings(ValueHashTable* t);

#endif
