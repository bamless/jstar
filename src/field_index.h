#ifndef FIELD_INDEX_H
#define FIELD_INDEX_H

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include "jstar.h"

struct ObjString;

typedef struct FieldIndexEntry {
    struct ObjString* key;
    int offset;
} FieldIndexEntry;

typedef struct FieldIndex {
    size_t sizeMask, numEntries;
    FieldIndexEntry* entries;
} FieldIndex;

void initFieldIndex(FieldIndex* t);
void freeFieldIndex(FieldIndex* t);
bool fieldIndexPut(FieldIndex* t, struct ObjString* key, int val);
bool fieldIndexGet(const FieldIndex* t, struct ObjString* key, int* res);
bool fieldIndexContainsKey(const FieldIndex* t, struct ObjString* key);
bool fieldIndexDel(FieldIndex* t, struct ObjString* key);
void fieldIndexMerge(FieldIndex* t, const FieldIndex* o);
struct ObjString* fieldIndexGetString(const FieldIndex* t, const char* str, size_t length,
                                      uint32_t hash);

void reachFieldIndex(JStarVM* vm, const FieldIndex* t);

#endif
