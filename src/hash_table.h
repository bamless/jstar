#ifndef HASHTABLE_H
#define HASHTABLE_H

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include "jstar.h"
#include "value.h"

typedef struct Entry {
    ObjString* key;
    Value value;
} Entry;

typedef struct HashTable {
    size_t sizeMask, numEntries;
    Entry* entries;
} HashTable;

// Initialize the hashtable
void initHashTable(HashTable* t);
// Free all resources associated with the hashtables
void freeHashTable(HashTable* t);
// Puts a Value associated with "key" in the hashtable
bool hashTablePut(HashTable* t, ObjString* key, Value val);
// Gets the value associated with "key" from the hashtable
bool hashTableGet(const HashTable* t, ObjString* key, Value* res);
// Returns true if the hashtable contains "key", false otherwise
bool hashTableContainsKey(const HashTable* t, ObjString* key);
// Deletes the value associated with "key" from the hashtable
bool hashTableDel(HashTable* t, ObjString* key);
// Adds all key/value pairs in o to t
void hashTableMerge(HashTable* t, const HashTable* o);
// Gets a ObjString* given a C string and its hash (used to implement a string pool)
ObjString* hashTableGetString(const HashTable* t, const char* str, size_t length, uint32_t hash);

void reachHashTable(JStarVM* vm, const HashTable* t);
void sweepStrings(HashTable* t);

#endif
