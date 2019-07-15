#ifndef HASHTABLE_H
#define HASHTABLE_H

#include "value.h"

#include <stdlib.h>

typedef struct ObjString ObjString;

#define MAX_LOAD_FACTOR  0.75
#define GROW_FACTOR      2
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

// Initialize the hashtable
void initHashTable(HashTable *t);
// Free all resources associated with the hashtables
void freeHashTable(HashTable *t);
// Puts a Value associated with "key" in the hashtable
bool hashTablePut(HashTable *t, ObjString *key, Value val);
// Gets the value associated with "key" from the hashtable
bool hashTableGet(HashTable *t, ObjString *key, Value *res);
// Returns true if the hashtable contains "key", false otherwise
bool hashTableContainsKey(HashTable *t, ObjString *key);
// Deletes the value associated with "key" from the hashtable
bool hashTableDel(HashTable *t, ObjString *key);
// Adds all key/value pairs in o to t
void hashTableMerge(HashTable *t, HashTable *o);
// Similar to merge, but doesn't add entries with a key starting with an underscore.
void hashTableImportNames(HashTable *t, HashTable *o);

// Gets a ObjString* given a C string and its hash (used to implement a string pool)
ObjString *HashTableGetString(HashTable *t, const char *str, size_t length, uint32_t hash);

// Removes all unreached strings in the hashtable
void removeUnreachedStrings(HashTable *t);

#endif
