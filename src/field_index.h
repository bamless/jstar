#ifndef FIELD_INDEX_H
#define FIELD_INDEX_H

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include "jstar.h"

typedef struct ObjString ObjString;

typedef struct FieldIndexEntry {
    ObjString* key;
    int offset;
} FieldIndexEntry;

typedef struct FieldIndex {
    size_t sizeMask, numEntries;
    FieldIndexEntry* entries;
} FieldIndex;

void initFieldIndex(FieldIndex* t);
void freeFieldIndex(FieldIndex* t);
bool fieldIndexPut(FieldIndex* t, ObjString* key, int val);
bool fieldIndexGet(const FieldIndex* t, ObjString* key, int* res);
bool fieldIndexContainsKey(const FieldIndex* t, ObjString* key);
bool fieldIndexDel(FieldIndex* t, ObjString* key);
void fieldIndexMerge(FieldIndex* t, FieldIndex* o);
ObjString* fieldIndexGetString(const FieldIndex* t, const char* str, size_t length, uint32_t hash);

void reachFieldIndex(JStarVM* vm, const FieldIndex* t);

#endif
