#include "memory.h"
#include "chunk.h"
#include "hashtable.h"
#include "vm.h"

#include <stdint.h>

#define REACHED_DEFAULT_SZ 16
#define REACHED_GROW_RATE   2

#define INIT_GC 1024 * 1024 // 1MiB
#define HEAP_GROW_RATE 2

#ifdef DBG_PRINT_GC
#include <stdio.h>
#include <string.h>
#endif

void freeObjects(MemManager *m);

void initMemoryManager(MemManager *m, VM *vm) {
	m->nextGC = INIT_GC;
	m->allocated = 0;
	m->objects = NULL;
	m->disableGC = false;
	m->vm = vm;
	m->reachedStack = NULL;
	m->reachedCapacity = 0;
	m->reachedCount = 0;
}

void freeMemoryManager(MemManager *m) {
	freeObjects(m);

	#ifdef DBG_PRINT_GC
	printf("Allocated at exit: %lu bytes\n", m->allocated);
	#endif
}

static Obj *newObj(MemManager *m, size_t size, ObjType type) {
	Obj *o = ALLOC(m, size);
	o->type = type;
	o->reached = false;
	o->next = m->objects;
	m->objects = o;
	return o;
}

static void garbageCollect(MemManager *m);

void *allocate(MemManager *m, void *ptr, size_t oldsize, size_t size) {
	m->allocated += size - oldsize;
	if(size > oldsize && !m->disableGC) {
		#ifdef DBG_STRESS_GC
		garbageCollect(m);
		#endif

		if(m->allocated > m->nextGC) {
			garbageCollect(m);
			m->nextGC = m->allocated * HEAP_GROW_RATE;
		}
	}

	return realloc(ptr, size);
}

static uint32_t hashString(const char *str);

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

#ifdef DBG_PRINT_GC
static void printObj(Obj *o) {
	switch(o->type) {
	case OBJ_STRING:
		printf("%s\n", ((ObjString*)o)->data);
		break;
	case OBJ_NATIVE:
		printf("native function %s arity: %d\n",
				((ObjNative*)o)->name->data, ((ObjNative*)o)->argsCount);
		break;
	case OBJ_FUNCTION:
		printf("function %s arity: %d\n",
				((ObjFunction*)o)->name->data, ((ObjFunction*)o)->argsCount);
		break;
	}
}
#endif

static void freeObject(MemManager *m, Obj *o) {
	switch(o->type) {
	case OBJ_STRING: {
		ObjString *s = (ObjString*) o;
		FREEARRAY(m, char, s->data, s->length + 1);
		FREE(m, ObjString, s);
		break;
	}
	case OBJ_NATIVE: {
		ObjNative *n = (ObjNative*) o;
		FREE(m, ObjNative, n);
		break;
	}
	case OBJ_FUNCTION: {
		ObjFunction *f = (ObjFunction*) o;
		freeChunk(&f->chunk);
		FREE(m, ObjFunction, f);
		break;
	}
	}
}

void freeObjects(MemManager *m) {
	Obj **head = &m->objects;
	while(*head != NULL) {
		if(!(*head)->reached)  {
			Obj *u = *head;
			*head = u->next;

			#ifdef DBG_PRINT_GC
			printf("FREE: unreached object %p type: %s repr: ", (void*)u, typeName[u->type]);
			printObj(u);
			#endif

			freeObject(m, u);
		} else {
			(*head)->reached = false;
			head = &(*head)->next;
		}
	}
}

void disableGC(MemManager *m , bool disable) {
	m->disableGC = disable;
}

static void growReached(MemManager *m) {
	m->reachedCapacity *= REACHED_GROW_RATE;
	m->reachedStack = realloc(m->reachedStack, m->reachedCapacity);
}

static void addReachedObject(MemManager *m, Obj *o) {
	if(m->reachedCount + 1 > m->reachedCapacity)
		growReached(m);
	m->reachedStack[m->reachedCount++] = o;
}

void reachObject(MemManager *m, Obj *o) {
	if(o == NULL || o->reached) return;

	#ifdef DBG_PRINT_GC
	printf("REACHED: Object %p type: %s repr: ", (void*)o, typeName[o->type]);
	printObj(o);
	#endif

	o->reached = true;
	addReachedObject(m, o);
}

void reachValue(MemManager *m, Value v) {
	if(IS_OBJ(v)) reachObject(m, AS_OBJ(v));
}

static void recursevelyReach(MemManager *m, Obj *o) {
	#ifdef DBG_PRINT_GC
	printf("Recursevely exploring object %p...\n", (void*)o);
	#endif

	switch(o->type) {
	case OBJ_NATIVE:
		reachObject(m, (Obj*)((ObjNative*)o)->name);
		break;
	case OBJ_FUNCTION:
		reachObject(m, (Obj*)((ObjFunction*)o)->name);
		break;
	default: break;
	}

	#ifdef DBG_PRINT_GC
	printf("End recursive exploring of object %p\n", (void*)o);
	#endif
}

static void garbageCollect(MemManager *m) {
	#ifdef DBG_PRINT_GC
	size_t prevAlloc = m->allocated;
	puts("**** Starting GC ****");
	#endif

	//init reached object stack
	m->reachedStack = malloc(sizeof(Obj*) * REACHED_DEFAULT_SZ);
	m->reachedCapacity = REACHED_DEFAULT_SZ;

	//reach roots
	VM *vm = m->vm;

	reachHashTable(m, &vm->globals);
	reachHashTable(m, &vm->strings);
	for(Value *v = vm->stack; v < vm->sp; v++) {
		reachValue(m, *v);
	}

	//recursevely reach objs held by other reached objs
	while(m->reachedCount != 0) {
		recursevelyReach(m, m->reachedStack[--m->reachedCount]);
	}

	//free the garbage
	freeObjects(m);

	//free the reached object's stack
	free(m->reachedStack);
	m->reachedStack = NULL;
	m->reachedCapacity = 0;
	m->reachedCount = 0;

	#ifdef DBG_PRINT_GC
	printf("Completed GC, prev allocated: %lu, curr allocated %lu, freed: %lu "
		"bytes of memory\n", prevAlloc, m->allocated, prevAlloc - m->allocated);
	puts("**** End of GC ****\n");
	#endif
}

static uint32_t hashString(const char *str) {
	uint32_t hash = 5381;

	int c;
	while((c = *str++)) {
		hash = ((hash << 5) + hash) + c; /* hash * 33 + c */
	}

	return hash;
}
