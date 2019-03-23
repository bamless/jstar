#ifndef HASHTABLE_H
#define HASHTABLE_H

#include "value.h"

#include <stdlib.h>

typedef struct ObjString ObjString;

#define MAX_LOAD_FACTOR 0.75
#define GROW_FACTOR 2
#define INITIAL_CAPACITY 16

typedef struct Entry {
	struct Entry *next;
	ObjString *key;
	Value value;
} Entry;

typedef struct HashTable {
	size_t size;
	size_t mask;
	size_t numEntries;
	Entry **entries;
} HashTable;

void initHashTable(HashTable *t);
void freeHashTable(HashTable *t);
bool hashTablePut(HashTable *t, ObjString *key, Value val);
bool hashTableGet(HashTable *t, ObjString *key, Value *res);
bool hashTableContainsKey(HashTable *t, ObjString *key);
bool hashTableDel(HashTable *t, ObjString *key);
void hashTableMerge(HashTable *t, HashTable *o);

// Similar to merge, but doesn't merge entries with a key starting with an underscore.
void hashTableImportNames(HashTable *t, HashTable *o);

ObjString *HashTableGetString(HashTable *t, const char *str, size_t length, uint32_t hash);

void removeUnreachedStrings(HashTable *t);

#endif
