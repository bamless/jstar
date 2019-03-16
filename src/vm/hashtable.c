#include "hashtable.h"
#include "memory.h"
#include "object.h"

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
	t->size = 0;
	t->mask = 0;
	t->entries = NULL;
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
	size_t index = stringGetHash(e->key) & t->mask;
	e->next = t->entries[index];
	t->entries[index] = e;
}

static Entry *getEntry(HashTable *t, ObjString *key) {
	if(t->entries == NULL) return NULL;

	size_t index = stringGetHash(key) & t->mask;

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

	t->size = t->size == 0 ? INITIAL_CAPACITY : t->size * GROW_FACTOR;
	t->entries = calloc(sizeof(Entry *), t->size);
	t->mask = t->size - 1;

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

bool hashTablePut(HashTable *t, ObjString *key, Value val) {
	Entry *e = getEntry(t, key);
	if(e == NULL) {
		if(t->numEntries + 1 > t->size * MAX_LOAD_FACTOR)
			grow(t);

		e = newEntry(key, val);
		addEntry(t, e);
		t->numEntries++;

		return true;
	}

	e->value = val;
	return false;
}

bool hashTableGet(HashTable *t, ObjString *key, Value *res) {
	Entry *e = getEntry(t, key);
	if(e != NULL) {
		*res = e->value;
		return true;
	}
	return false;
}

bool hashTableContainsKey(HashTable *t, ObjString *key) {
	return getEntry(t, key) != NULL;
}

bool hashTableDel(HashTable *t, ObjString *key) {
	size_t index = stringGetHash(key) & t->mask;

	Entry **buckHead = &t->entries[index];
	while(*buckHead != NULL) {
		if(keyEquals(key, (*buckHead)->key)) {
			Entry *f = *buckHead;
			*buckHead = f->next;

			free(f);
			t->numEntries--;
			return true;
		} else {
			buckHead = &(*buckHead)->next;
		}
	}

	return false;
}

void hashTableMerge(HashTable *t, HashTable *o) {
	for(size_t i = 0; i < o->size; i++) {
		Entry *head = o->entries[i];
		while(head != NULL) {
			hashTablePut(t, head->key, head->value);
			head = head->next;
		}
	}
}

void hashTableImportNames(HashTable *t, HashTable *o) {
	for(size_t i = 0; i < o->size; i++) {
		Entry *head = o->entries[i];
		while(head != NULL) {
			if(head->key->data[0] != '_') {
				hashTablePut(t, head->key, head->value);
			}
			head = head->next;
		}
	}
}

ObjString *HashTableGetString(HashTable *t, const char *str, size_t length, uint32_t hash) {
	if(t->entries == NULL) return NULL;

	size_t index = hash & t->mask;

	Entry *buckHead = t->entries[index];
	while(buckHead != NULL) {
		if(length == buckHead->key->length
			&& memcmp(str, buckHead->key->data, length) == 0) {
			return buckHead->key;
		}
		buckHead = buckHead->next;
	}

	return NULL;
}

void removeUnreachedStrings(HashTable *t) {
	for(size_t i = 0; i < t->size; i++) {
		Entry *buckHead = t->entries[i];
		while(buckHead != NULL) {
			Entry *f = buckHead;
			buckHead = buckHead->next;
			if(!f->key->base.reached) {
				hashTableDel(t, f->key);
			}
		}
	}
}
