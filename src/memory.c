#include "memory.h"
#include "chunk.h"

#include <stdint.h>

void freeObjects(MemManager *m);

void initMemoryManager(MemManager *m, VM *vm) {
	m->allocated = 0;
	m->objects = NULL;
	m->disableGC = false;
	m->vm = vm;
}

void freeMemoryManager(MemManager *m) {
	freeObjects(m);
}

static uint32_t hashString(const char *str);

static Obj *newObj(MemManager *m, size_t size, ObjType type) {
	Obj *o = allocate(m, NULL, 0, size);
	o->type = type;
	o->dark = false;
	o->next = m->objects;
	m->objects = o;
	return o;
}

void *allocate(MemManager *m, void *ptr, size_t oldsize, size_t size) {
	m->allocated += size - oldsize;

	//TODO: here garbage collect
	return realloc(ptr, size);
}

ObjString *newString(MemManager *m, char *cstring, size_t length) {
	ObjString *str = (ObjString*) newObj(m, sizeof(*str), OBJ_STRING);
	str->length = length;
	str->data = cstring;
	str->hash = hashString(cstring);
	return str;
}

ObjFunction *newFunction(MemManager *m, int argsCount) {
	ObjFunction *f = (ObjFunction*) newObj(m, sizeof(*f), OBJ_FUNCTION);
	f->argsCount = argsCount;
	f->name = NULL;
	initChunk(&f->chunk);
	return f;
}

ObjNative *newNative(MemManager *m, int argsCount, Native fn) {
	ObjNative *n = (ObjNative*) newObj(m, sizeof(*n), OBJ_NATIVE);
	n->argsCount = argsCount;
	n->name = NULL;
	n->fn = fn;
	return n;
}

void disableGC(MemManager *m , bool disable) {
	m->disableGC = disable;
}

void freeObject(Obj *o) {
	//TODO: free recursively the pbject
}

void freeObjects(MemManager *m) {
	Obj *head = m->objects;
	while(head != NULL) {
		Obj *f = head;
		head = head->next;

		if(!f->dark) freeObject(f);
	}
}

static uint32_t hashString(const char *str) {
	uint32_t hash = 5381;

	int c;
	while((c = *str++)) {
		hash = ((hash << 5) + hash) + c; /* hash * 33 + c */
	}

	return hash;
}
