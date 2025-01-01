// TODO: this is basically the same as 'hasjhable.c'. It would be ideal if we can unify the 2 via
// generic macro programming (akin to `parse/vector.h`). For now we leave it as is for convienience.

#include "field_index.h"

#include <stdbool.h>
#include <string.h>

#include "gc.h"
#include "object.h"

#define TOMB_MARKER      -1
#define INVALID_OFF      -2
#define GROW_FACTOR      2
#define INITIAL_CAPACITY 8
#define MAX_ENTRY_LOAD(size) \
    (((size) >> 1) + ((size) >> 2))  // Read as: 3 / 4 * size i.e. a load factor of 75%

void initFieldIndex(FieldIndex* t) {
    *t = (FieldIndex){0};
}

void freeFieldIndex(FieldIndex* t) {
    free(t->entries);
}

static FieldIndexEntry* findFieldIndexEntry(FieldIndexEntry* entries, size_t sizeMask,
                                            ObjString* key) {
    size_t i = stringGetHash(key) & sizeMask;
    FieldIndexEntry* tomb = NULL;

    for(;;) {
        FieldIndexEntry* e = &entries[i];
        if(!e->key) {
            if(e->offset == INVALID_OFF) {
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

static void growEntries(FieldIndex* t) {
    size_t newSize = t->sizeMask ? (t->sizeMask + 1) * GROW_FACTOR : INITIAL_CAPACITY;
    FieldIndexEntry* newEntries = malloc(sizeof(FieldIndexEntry) * newSize);

    for(size_t i = 0; i < newSize; i++) {
        newEntries[i] = (FieldIndexEntry){NULL, INVALID_OFF};
    }

    t->numEntries = 0;
    if(t->sizeMask != 0) {
        for(size_t i = 0; i <= t->sizeMask; i++) {
            FieldIndexEntry* e = &t->entries[i];
            if(!e->key) continue;

            FieldIndexEntry* dest = findFieldIndexEntry(newEntries, newSize - 1, e->key);
            *dest = (FieldIndexEntry){e->key, e->offset};
            t->numEntries++;
        }
    }

    free(t->entries);
    t->entries = newEntries;
    t->sizeMask = newSize - 1;
}

bool fieldIndexPut(FieldIndex* t, ObjString* key, int val) {
    if(t->numEntries + 1 > MAX_ENTRY_LOAD(t->sizeMask + 1)) {
        growEntries(t);
    }

    FieldIndexEntry* e = findFieldIndexEntry(t->entries, t->sizeMask, key);

    // is it a true empty entry or a tombstone?
    bool newFieldIndexEntry = !e->key;

    if(newFieldIndexEntry && e->offset == INVALID_OFF) {
        t->numEntries++;
    }

    *e = (FieldIndexEntry){key, val};
    return newFieldIndexEntry;
}

bool fieldIndexGet(const FieldIndex* t, ObjString* key, int* res) {
    if(t->entries == NULL) return false;
    FieldIndexEntry* e = findFieldIndexEntry(t->entries, t->sizeMask, key);
    if(!e->key) return false;
    *res = e->offset;
    return true;
}

bool fieldIndexContainsKey(const FieldIndex* t, ObjString* key) {
    if(t->entries == NULL) return false;
    return findFieldIndexEntry(t->entries, t->sizeMask, key)->key != NULL;
}

bool fieldIndexDel(FieldIndex* t, ObjString* key) {
    if(t->numEntries == 0) return false;
    FieldIndexEntry* e = findFieldIndexEntry(t->entries, t->sizeMask, key);
    if(!e->key) return false;
    *e = (FieldIndexEntry){NULL, TOMB_MARKER};
    return true;
}

void fieldIndexMerge(FieldIndex* t, const FieldIndex* o) {
    if(o->entries == NULL) return;
    for(size_t i = 0; i <= o->sizeMask; i++) {
        FieldIndexEntry* e = &o->entries[i];
        if(e->key) {
            fieldIndexPut(t, e->key, e->offset);
        }
    }
}

ObjString* fieldIndexGetString(const FieldIndex* t, const char* str, size_t length, uint32_t hash) {
    if(t->entries == NULL) return NULL;
    size_t i = hash & t->sizeMask;
    for(;;) {
        FieldIndexEntry* e = &t->entries[i];
        if(!e->key) {
            if(e->offset == INVALID_OFF) return NULL;
        } else if(stringGetHash(e->key) == hash && e->key->length == length &&
                  memcmp(e->key->data, str, length) == 0) {
            return e->key;
        }
        i = (i + 1) & t->sizeMask;
    }
}

void reachFieldIndex(JStarVM* vm, const FieldIndex* t) {
    if(t->entries == NULL) return;
    for(size_t i = 0; i <= t->sizeMask; i++) {
        FieldIndexEntry* e = &t->entries[i];
        reachObject(vm, (Obj*)e->key);
    }
}

void reachFieldIndexKeys(JStarVM* vm, const FieldIndex* t) {
    if(t->entries == NULL) return;
    for(size_t i = 0; i <= t->sizeMask; i++) {
        FieldIndexEntry* e = &t->entries[i];
        reachObject(vm, (Obj*)e->key);
    }
}
