#include "hashtable.h"

#include <stdbool.h>
#include <string.h>

#include "gc.h"
#include "object.h"
#include "profiler.h"

#define TOMB_MARKER      TRUE_VAL
#define GROW_FACTOR      2
#define INITIAL_CAPACITY 8
#define MAX_ENTRY_LOAD(size) \
    (((size) >> 1) + ((size) >> 2))  // Read as: 3 / 4 * size i.e. a load factor of 75%

void initHashTable(HashTable* t) {
    *t = (HashTable){0};
}

void freeHashTable(HashTable* t) {
    free(t->entries);
}

static Entry* findEntry(Entry* entries, size_t sizeMask, ObjString* key) {
    size_t i = stringGetHash(key) & sizeMask;
    Entry* tomb = NULL;

    for(;;) {
        Entry* e = &entries[i];
        if(!e->key) {
            if(IS_NULL(e->value)) {
                return tomb ? tomb : e;
            } else if(!tomb) {
                tomb = e;
            }
        } else if(stringEquals(e->key, key)) {
            return e;
        }
        i = (i + 1) & sizeMask;
    }
}

static void growEntries(HashTable* t) {
    size_t newSize = t->sizeMask ? (t->sizeMask + 1) * GROW_FACTOR : INITIAL_CAPACITY;
    Entry* newEntries = malloc(sizeof(Entry) * newSize);

    for(size_t i = 0; i < newSize; i++) {
        newEntries[i] = (Entry){NULL, NULL_VAL};
    }

    t->numEntries = 0;
    if(t->sizeMask != 0) {
        for(size_t i = 0; i <= t->sizeMask; i++) {
            Entry* e = &t->entries[i];
            if(!e->key) continue;

            Entry* dest = findEntry(newEntries, newSize - 1, e->key);
            *dest = (Entry){e->key, e->value};
            t->numEntries++;
        }
    }

    free(t->entries);
    t->entries = newEntries;
    t->sizeMask = newSize - 1;
}

bool hashTablePut(HashTable* t, ObjString* key, Value val) {
    if(t->numEntries + 1 > MAX_ENTRY_LOAD(t->sizeMask + 1)) {
        growEntries(t);
    }

    Entry* e = findEntry(t->entries, t->sizeMask, key);

    // is it a true empty entry or a tombstone?
    bool newEntry = !e->key;

    if(newEntry && IS_NULL(e->value)) {
        t->numEntries++;
    }

    *e = (Entry){key, val};
    return newEntry;
}

bool hashTableGet(const HashTable* t, ObjString* key, Value* res) {
    if(t->entries == NULL) return false;
    Entry* e = findEntry(t->entries, t->sizeMask, key);
    if(!e->key) return false;
    *res = e->value;
    return true;
}

bool hashTableContainsKey(const HashTable* t, ObjString* key) {
    if(t->entries == NULL) return false;
    return findEntry(t->entries, t->sizeMask, key)->key != NULL;
}

bool hashTableDel(HashTable* t, ObjString* key) {
    if(t->numEntries == 0) return false;
    Entry* e = findEntry(t->entries, t->sizeMask, key);
    if(!e->key) return false;
    *e = (Entry){NULL, TOMB_MARKER};
    return true;
}

void hashTableMerge(HashTable* t, HashTable* o) {
    if(o->entries == NULL) return;
    for(size_t i = 0; i <= o->sizeMask; i++) {
        Entry* e = &o->entries[i];
        if(e->key) {
            hashTablePut(t, e->key, e->value);
        }
    }
}

ObjString* hashTableGetString(const HashTable* t, const char* str, size_t length, uint32_t hash) {
    if(t->entries == NULL) return NULL;
    size_t i = hash & t->sizeMask;
    for(;;) {
        Entry* e = &t->entries[i];
        if(!e->key) {
            if(IS_NULL(e->value)) return NULL;
        } else if(stringGetHash(e->key) == hash && e->key->length == length &&
                  memcmp(e->key->data, str, length) == 0) {
            return e->key;
        }
        i = (i + 1) & t->sizeMask;
    }
}

void reachHashTable(JStarVM* vm, const HashTable* t) {
    if(t->entries == NULL) return;
    for(size_t i = 0; i <= t->sizeMask; i++) {
        Entry* e = &t->entries[i];
        reachObject(vm, (Obj*)e->key);
        reachValue(vm, e->value);
    }
}

void reachHashTableKeys(JStarVM* vm, const HashTable* t) {
    if(t->entries == NULL) return;
    for(size_t i = 0; i <= t->sizeMask; i++) {
        Entry* e = &t->entries[i];
        reachObject(vm, (Obj*)e->key);
    }
}

void sweepStrings(HashTable* t) {
    PROFILE_FUNC()

    if(t->entries == NULL) return;
    for(size_t i = 0; i <= t->sizeMask; i++) {
        Entry* e = &t->entries[i];
        if(e->key && !e->key->base.reached) {
            *e = (Entry){NULL, TOMB_MARKER};
        }
    }
}
