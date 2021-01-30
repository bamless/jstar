#include "hashtable.h"

#include <stdbool.h>
#include <string.h>

#include "gc.h"
#include "object.h"

#define MAX_LOAD_FACTOR  0.75
#define GROW_FACTOR      2
#define INITIAL_CAPACITY 8

void initHashTable(HashTable* t) {
    t->sizeMask = 0;
    t->numEntries = 0;
    t->entries = NULL;
}

void freeHashTable(HashTable* t) {
    free(t->entries);
}

static Entry* findEntry(Entry* entries, size_t sizeMask, ObjString* key) {
    size_t i = STRING_GET_HASH(key) & sizeMask;
    Entry* tomb = NULL;

    for(;;) {
        Entry* e = &entries[i];
        if(e->key == NULL) {
            if(IS_NULL(e->value)) {
                return tomb ? tomb : e;
            } else if(!tomb) {
                tomb = e;
            }
        } else if(STRING_EQUALS(e->key, key)) {
            return e;
        }
        i = (i + 1) & sizeMask;
    }
}

static void growEntries(HashTable* t) {
    size_t newSize = t->sizeMask ? (t->sizeMask + 1) * GROW_FACTOR : INITIAL_CAPACITY;
    Entry* newEntries = malloc(sizeof(Entry) * newSize);

    for(size_t i = 0; i < newSize; i++) {
        newEntries[i].key = NULL;
        newEntries[i].value = NULL_VAL;
    }

    t->numEntries = 0;
    if(t->sizeMask != 0) {
        for(size_t i = 0; i <= t->sizeMask; i++) {
            Entry* e = &t->entries[i];
            if(e->key == NULL) continue;

            Entry* dest = findEntry(newEntries, newSize - 1, e->key);
            dest->key = e->key;
            dest->value = e->value;
            
            t->numEntries++;
        }
    }

    free(t->entries);
    t->entries = newEntries;
    t->sizeMask = newSize - 1;
}

bool hashTablePut(HashTable* t, ObjString* key, Value val) {
    if(t->numEntries + 1 > (t->sizeMask + 1) * MAX_LOAD_FACTOR) {
        growEntries(t);
    }

    Entry* e = findEntry(t->entries, t->sizeMask, key);
    bool isNew = e->key == NULL;
    if(isNew && IS_NULL(e->value)) {
        t->numEntries++;
    }

    e->key = key;
    e->value = val;
    return isNew;
}

bool hashTableGet(HashTable* t, ObjString* key, Value* res) {
    if(t->entries == NULL) return false;
    Entry* e = findEntry(t->entries, t->sizeMask, key);
    if(e->key == NULL) return false;
    *res = e->value;
    return true;
}

bool hashTableContainsKey(HashTable* t, ObjString* key) {
    if(t->entries == NULL) return false;
    return findEntry(t->entries, t->sizeMask, key)->key != NULL;
}

bool hashTableDel(HashTable* t, ObjString* key) {
    if(t->numEntries == 0) return false;
    Entry* e = findEntry(t->entries, t->sizeMask, key);
    if(e->key == NULL) return false;
    e->key = NULL;
    e->value = TRUE_VAL;
    return true;
}

void hashTableMerge(HashTable* t, HashTable* o) {
    if(o->entries == NULL) return;
    for(size_t i = 0; i <= o->sizeMask; i++) {
        Entry* e = &o->entries[i];
        if(e->key != NULL) {
            hashTablePut(t, e->key, e->value);
        }
    }
}

void hashTableImportNames(HashTable* t, HashTable* o) {
    if(o->entries == NULL) return;
    for(size_t i = 0; i <= o->sizeMask; i++) {
        Entry* e = &o->entries[i];
        if(e->key != NULL && e->key->data[0] != '_') {
            hashTablePut(t, e->key, e->value);
        }
    }
}

ObjString* hashTableGetString(HashTable* t, const char* str, size_t length, uint32_t hash) {
    if(t->entries == NULL) return NULL;
    size_t i = hash & t->sizeMask;
    for(;;) {
        Entry* e = &t->entries[i];
        if(e->key == NULL) {
            if(IS_NULL(e->value)) return NULL;
        } else if(STRING_GET_HASH(e->key) == hash && e->key->length == length &&
                  memcmp(e->key->data, str, length) == 0) {
            return e->key;
        }
        i = (i + 1) & t->sizeMask;
    }
}

void reachHashTable(JStarVM* vm, HashTable* t) {
    if(t->entries == NULL) return;
    for(size_t i = 0; i <= t->sizeMask; i++) {
        Entry* e = &t->entries[i];
        reachObject(vm, (Obj*)e->key);
        reachValue(vm, e->value);
    }
}

void sweepStrings(HashTable* t) {
    if(t->entries == NULL) return;
    for(size_t i = 0; i <= t->sizeMask; i++) {
        Entry* e = &t->entries[i];
        if(e->key != NULL && !e->key->base.reached) {
            hashTableDel(t, e->key);
        }
    }
}
