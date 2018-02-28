#include "hashtable.h"

#include <stdbool.h>
#include <string.h>

static Entry *newEntry(ObjString *key, Value val) {
	Entry *e = malloc(sizeof(*e));
	e->next = NULL;
	e->key = key;
	e->value = val;
	return e;
}

void initHashTable(HashTable *t) {
	t->numEntries = 0;
	t->size = INITIAL_CAPACITY;
	t->entries = calloc(sizeof(Entry *), INITIAL_CAPACITY);
}

void freeHashTable(HashTable *t) {
	for(size_t i = 0; i < t->size; i++) {
		Entry *buckHead = t->entries[i];
		while(buckHead != NULL) {
			Entry *f = buckHead;
			buckHead = buckHead->next;
			free(f);
		}
	}

	free(t->entries);
}

static bool keyEquals(ObjString *k1, ObjString *k2) {
	return strcmp(k1->data, k2->data) == 0;
}

static void addEntry(HashTable *t, Entry *e) {
	size_t index = e->key->hash % t->size;
	e->next = t->entries[index];
	t->entries[index] = e;
}

static Entry *getEntry(HashTable *t, ObjString *key) {
	size_t index = key->hash % t->size;

	Entry *buckHead = t->entries[index];
	while(buckHead != NULL) {
		if(keyEquals(key, buckHead->key)) {
			return buckHead;
		}
		buckHead = buckHead->next;
	}

	return NULL;
}

static void grow(HashTable *t) {
	size_t oldSize = t->size;
	Entry **oldEntries = t->entries;

	t->size *= GROW_FACTOR;
	t->entries = calloc(sizeof(Entry *), t->size);

	for(size_t i = 0; i < oldSize; i++) {
		Entry *buckHead = oldEntries[i];
		while(buckHead != NULL) {
			Entry *e = buckHead;
			buckHead = buckHead->next;

			addEntry(t, e);
		}
	}

	free(oldEntries);
}

void hashTablePut(HashTable *t, ObjString *key, Value val) {
	Entry *e = getEntry(t, key);
	if(e == NULL) {
		if(t->numEntries + 1 > t->size * MAX_LOAD_FACTOR)
			grow(t);

		e = newEntry(key, val);
		addEntry(t, e);
		t->numEntries++;
	} else {
		e->value = val;
	}
}

bool hashTableGet(HashTable *t, ObjString *key, Value *res) {
	Entry *e = getEntry(t, key);
	if(e != NULL) *res = e->value;

	return e != NULL;
}

bool hashTableDel(HashTable *t, ObjString *key) {
	size_t index = key->hash % t->size;

	Entry *prev = NULL;
	Entry *buckHead = t->entries[index];
	while(buckHead != NULL) {
		if(keyEquals(key, buckHead->key)) {
			if(prev != NULL) prev->next = buckHead->next;
			else t->entries[index] = buckHead->next;

			free(buckHead);
			t->numEntries--;
			return true;
		}
		prev = buckHead;
		buckHead = buckHead->next;
	}

	return false;
}
