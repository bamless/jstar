#ifndef HASHTABLE_H
#define HASHTABLE_H

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>  // IWYU pragma: keep
#include <string.h>

/**
 * Generic hash table implementation that maps ObjString* to values of type V.
 *
 * Use `DECLARE_HASH_TABLE(name, V)` in an header file to declare a new hash table that maps
 * ObjString* to values of type V.
 *
 * Then, use `DEFINE_HASH_TABLE(name, V, ...)` in a c file to generate the implementation.
 */

#define DECLARE_HASH_TABLE(name, V)                                                  \
    typedef struct name##Entry {                                                     \
        ObjString* key;                                                              \
        V value;                                                                     \
    } name##Entry;                                                                   \
                                                                                     \
    typedef struct name##HashTable {                                                 \
        size_t sizeMask, numEntries;                                                 \
        name##Entry* entries;                                                        \
    } name##HashTable;                                                               \
                                                                                     \
    void init##name##HashTable(name##HashTable* t);                                  \
    void free##name##HashTable(name##HashTable* t);                                  \
    bool hashTable##name##Put(name##HashTable* t, ObjString* key, V val);            \
    bool hashTable##name##Get(const name##HashTable* t, ObjString* key, V* res);     \
    bool hashTable##name##ContainsKey(const name##HashTable* t, ObjString* key);     \
    bool hashTable##name##Del(name##HashTable* t, ObjString* key);                   \
    void hashTable##name##Merge(name##HashTable* t, const name##HashTable* o);       \
    ObjString* hashTable##name##GetString(const name##HashTable* t, const char* str, \
                                          size_t length, uint32_t hash);

#define MAX_ENTRY_LOAD(size) \
    (((size) >> 1) + ((size) >> 2))  // Read as: size * 0.75, i.e. a load factor of 75%

#define DEFINE_HASH_TABLE(name, V, TOMB_MARKER, INVALID_VAL, IS_INVALID_VAL, GROW_FACTOR,  \
                          INITIAL_CAPACITY)                                                \
                                                                                           \
    JSR_STATIC_ASSERT(((INITIAL_CAPACITY) & ((INITIAL_CAPACITY) - 1)) == 0,                \
                      "Initial capacity must be a power of 2");                            \
                                                                                           \
    void init##name##HashTable(name##HashTable* t) {                                       \
        *t = (name##HashTable){0};                                                         \
    }                                                                                      \
                                                                                           \
    void free##name##HashTable(name##HashTable* t) {                                       \
        free(t->entries);                                                                  \
    }                                                                                      \
                                                                                           \
    static name##Entry* findEntry(name##Entry* entries, size_t sizeMask, ObjString* key) { \
        size_t i = stringGetHash(key) & sizeMask;                                          \
        name##Entry* tomb = NULL;                                                          \
                                                                                           \
        for(;;) {                                                                          \
            name##Entry* e = &entries[i];                                                  \
            if(!e->key) {                                                                  \
                if(IS_INVALID_VAL(e->value)) {                                             \
                    return tomb ? tomb : e;                                                \
                } else if(!tomb) {                                                         \
                    tomb = e;                                                              \
                }                                                                          \
            } else if(stringEquals(e->key, key)) {                                         \
                return e;                                                                  \
            }                                                                              \
            i = (i + 1) & sizeMask;                                                        \
        }                                                                                  \
    }                                                                                      \
                                                                                           \
    static void growEntries(name##HashTable* t) {                                          \
        size_t newSize = t->sizeMask ? (t->sizeMask + 1) * GROW_FACTOR : INITIAL_CAPACITY; \
        name##Entry* newEntries = malloc(sizeof(name##Entry) * newSize);                   \
                                                                                           \
        for(size_t i = 0; i < newSize; i++) {                                              \
            newEntries[i] = (name##Entry){NULL, INVALID_VAL};                              \
        }                                                                                  \
                                                                                           \
        t->numEntries = 0;                                                                 \
        if(t->sizeMask != 0) {                                                             \
            for(size_t i = 0; i <= t->sizeMask; i++) {                                     \
                name##Entry* e = &t->entries[i];                                           \
                if(!e->key) continue;                                                      \
                                                                                           \
                name##Entry* dest = find##Entry(newEntries, newSize - 1, e->key);          \
                *dest = (name##Entry){e->key, e->value};                                   \
                t->numEntries++;                                                           \
            }                                                                              \
        }                                                                                  \
                                                                                           \
        free(t->entries);                                                                  \
        t->entries = newEntries;                                                           \
        t->sizeMask = newSize - 1;                                                         \
    }                                                                                      \
                                                                                           \
    bool hashTable##name##Put(name##HashTable* t, ObjString* key, V val) {                 \
        if(t->numEntries + 1 > MAX_ENTRY_LOAD(t->sizeMask + 1)) {                          \
            growEntries(t);                                                                \
        }                                                                                  \
                                                                                           \
        name##Entry* e = findEntry(t->entries, t->sizeMask, key);                          \
        bool newEntry = !e->key;                                                           \
                                                                                           \
        if(newEntry && IS_INVALID_VAL(e->value)) {                                         \
            t->numEntries++;                                                               \
        }                                                                                  \
                                                                                           \
        *e = (name##Entry){key, val};                                                      \
        return newEntry;                                                                   \
    }                                                                                      \
                                                                                           \
    bool hashTable##name##Get(const name##HashTable* t, ObjString* key, V* res) {          \
        if(t->entries == NULL) return false;                                               \
        name##Entry* e = findEntry(t->entries, t->sizeMask, key);                          \
        if(!e->key) return false;                                                          \
        *res = e->value;                                                                   \
        return true;                                                                       \
    }                                                                                      \
                                                                                           \
    bool hashTable##name##ContainsKey(const name##HashTable* t, ObjString* key) {          \
        if(t->entries == NULL) return false;                                               \
        return findEntry(t->entries, t->sizeMask, key)->key != NULL;                       \
    }                                                                                      \
                                                                                           \
    bool hashTable##name##Del(name##HashTable* t, ObjString* key) {                        \
        if(t->numEntries == 0) return false;                                               \
        name##Entry* e = findEntry(t->entries, t->sizeMask, key);                          \
        if(!e->key) return false;                                                          \
        *e = (name##Entry){NULL, TOMB_MARKER};                                             \
        return true;                                                                       \
    }                                                                                      \
                                                                                           \
    void hashTable##name##Merge(name##HashTable* t, const name##HashTable* o) {            \
        if(o->entries == NULL) return;                                                     \
        for(size_t i = 0; i <= o->sizeMask; i++) {                                         \
            name##Entry* e = &o->entries[i];                                               \
            if(e->key) {                                                                   \
                hashTable##name##Put(t, e->key, e->value);                                 \
            }                                                                              \
        }                                                                                  \
    }                                                                                      \
                                                                                           \
    ObjString* hashTable##name##GetString(const name##HashTable* t, const char* str,       \
                                          size_t length, uint32_t hash) {                  \
        if(t->entries == NULL) return NULL;                                                \
        size_t i = hash & t->sizeMask;                                                     \
        for(;;) {                                                                          \
            name##Entry* e = &t->entries[i];                                               \
            if(!e->key) {                                                                  \
                if(IS_INVALID_VAL(e->value)) return NULL;                                  \
            } else if(stringGetHash(e->key) == hash && e->key->length == length &&         \
                      memcmp(e->key->data, str, length) == 0) {                            \
                return e->key;                                                             \
            }                                                                              \
            i = (i + 1) & t->sizeMask;                                                     \
        }                                                                                  \
    }

#endif
