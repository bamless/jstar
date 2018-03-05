#ifndef HASHTABLE_H
#define HASHTABLE_H

#include "object.h"
#include "value.h"
#include "memory.h"

#include <stdlib.h>

#define MAX_LOAD_FACTOR 0.2
#define GROW_FACTOR 2
#define INITIAL_CAPACITY 16

typedef struct Entry {
	struct Entry *next;
	ObjString *key;
	Value value;
} Entry;

typedef struct HashTable {
	size_t size;
	size_t numEntries;
	Entry **entries;
} HashTable;

void initHashTable(HashTable *t);
void freeHashTable(HashTable *t);
void hashTablePut(HashTable *t, ObjString *key, Value val);
bool hashTableGet(HashTable *t, ObjString *key, Value *res);
bool hashTableDel(HashTable *t, ObjString *key);

void reachHashTable(MemManager *m, HashTable *t);

#endif
