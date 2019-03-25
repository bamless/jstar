#include "memory.h"
#include "chunk.h"
#include "hashtable.h"
#include "compiler.h"
#include "options.h"
#include "util.h"
#include "vm.h"

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#define REACHED_DEFAULT_SZ 16
#define REACHED_GROW_RATE   2
#define HEAP_GROW_RATE      2

#ifdef DBG_PRINT_GC
#include <stdio.h>
#include <string.h>
#endif

#define GC_ALLOC(vm, size) GCallocate(vm, NULL, 0, size)

#define GC_FREE(vm, type, obj) GCallocate(vm, obj, sizeof(type), 0)
#define GC_FREEARRAY(vm, type, obj, count) GCallocate(vm, obj, sizeof(type) * count, 0)

#define GC_FREE_VAR(vm, type, vartype, count, obj) \
	GCallocate(vm, obj, sizeof(type) + sizeof(vartype) * count, 0)

static void *GCallocate(BlangVM *vm, void *ptr, size_t oldsize, size_t size) {
	vm->allocated += size - oldsize;
	if(size > oldsize && !vm->disableGC) {
#ifdef DBG_STRESS_GC
		garbageCollect(vm);
#endif

		if(vm->allocated > vm->nextGC) {
			garbageCollect(vm);
		}
	}

	if(size == 0) {
		free(ptr);
		return NULL;
	}

	void *mem = realloc(ptr, size);
	if(!mem) {
		perror("Error while allocating memory");
		abort();
	}

	return mem;
}

static Obj *newObj(BlangVM *vm, size_t size, ObjClass *cls, ObjType type) {
	Obj *o = GC_ALLOC(vm, size);
	o->cls = cls;
	o->type = type;
	o->reached = false;
	o->next = vm->objects;
	vm->objects = o;
	return o;
}

static Obj *newVarObj(BlangVM *vm, size_t size, size_t varSize, size_t count, ObjClass *cls, ObjType type) {
	return newObj(vm, size + varSize * count, cls, type);
}

ObjFunction *newFunction(BlangVM *vm, ObjModule *module, ObjString *name, uint8_t argc, uint8_t defaultc) {
	Value *defArr = defaultc > 0 ? GC_ALLOC(vm, sizeof(Value) * defaultc) : NULL;
	memset(defArr, 0, defaultc * sizeof(Value));

	ObjFunction *f = (ObjFunction*) newObj(vm, sizeof(*f), vm->funClass, OBJ_FUNCTION);
	f->argsCount = argc;
	f->defaultc = defaultc;
	f->vararg = false;
	f->defaults = defArr;
	f->module = module;
	f->upvaluec = 0;
	f->name = name;
	initChunk(&f->chunk);
	return f;
}

ObjNative *newNative(BlangVM *vm, ObjModule *module, ObjString *name, uint8_t argc, Native fn, uint8_t defaultc) {
	Value *defArr = defaultc > 0 ? GC_ALLOC(vm, sizeof(Value) * defaultc) : NULL;
	memset(defArr, 0, defaultc * sizeof(Value));

	ObjNative *n = (ObjNative*) newObj(vm, sizeof(*n), vm->funClass, OBJ_NATIVE);
	n->argsCount = argc;
	n->vararg = false;
	n->module = module;
	n->name = name;
	n->fn = fn;
	n->defaults = defArr;
	n->defaultc = defaultc;
	return n;
}

ObjClass *newClass(BlangVM *vm, ObjString *name, ObjClass *superCls) {
	ObjClass *cls = (ObjClass*) newObj(vm, sizeof(*cls), vm->clsClass, OBJ_CLASS);
	cls->name = name;
	cls->superCls = superCls;
	initHashTable(&cls->methods);
	return cls;
}
ObjInstance *newInstance(BlangVM *vm, ObjClass *cls) {
	ObjInstance *inst = (ObjInstance*) newObj(vm, sizeof(*inst), cls, OBJ_INST);
	initHashTable(&inst->fields);
	return inst;
}

ObjClosure *newClosure(BlangVM *vm, ObjFunction *fn) {
	ObjClosure *c = (ObjClosure*) newVarObj(vm, sizeof(*c), sizeof(ObjUpvalue*), fn->upvaluec, vm->funClass, OBJ_CLOSURE);
	memset(c->upvalues, 0, sizeof(ObjUpvalue*) * fn->upvaluec);
	c->upvalueCount = fn->upvaluec;
	c->fn = fn;
	return c;
}

ObjModule *newModule(BlangVM *vm, ObjString *name) {
	ObjModule *module = (ObjModule*) newObj(vm, sizeof(*module), vm->modClass, OBJ_MODULE);
	module->name = name;
	initHashTable(&module->globals);
	return module;
}

ObjUpvalue *newUpvalue(BlangVM *vm, Value *addr) {
	ObjUpvalue *upvalue = (ObjUpvalue*) newObj(vm, sizeof(*upvalue), NULL, OBJ_UPVALUE);
	upvalue->addr = addr;
	upvalue->closed = NULL_VAL;
	upvalue->next = NULL;
	return upvalue;
}

ObjBoundMethod *newBoundMethod(BlangVM *vm, Value b, Obj *method) {
	ObjBoundMethod *bound = (ObjBoundMethod*) newObj(vm, sizeof(*bound), vm->funClass, OBJ_BOUND_METHOD);
	bound->bound = b;
	bound->method = method;
	return bound;
}

ObjTuple *newTuple(BlangVM *vm, size_t size) {
	if(size == 0 && vm->emptyTup) return vm->emptyTup;

	ObjTuple *tuple = (ObjTuple*) newVarObj(vm, sizeof(*tuple), sizeof(Value), size, vm->tupClass, OBJ_TUPLE);
	tuple->size = size;
	for(uint8_t i = 0; i < tuple->size; i++) {
		tuple->arr[i] = NULL_VAL;
	}
	return tuple;
}

#define ST_DEF_SIZE 16

ObjStackTrace *newStackTrace(BlangVM *vm) {
	char *trace = GC_ALLOC(vm, sizeof(char) * ST_DEF_SIZE);
	ObjStackTrace *st = (ObjStackTrace*) newObj(vm, sizeof(*st), vm->stClass, OBJ_STACK_TRACE);
	st->size = ST_DEF_SIZE;
	st->length = 0;
	st->trace = trace;
	st->trace[0] = '\0';
	st->lastTracedFrame = -1;
	return st;
}

static void growStackTrace(BlangVM *vm, ObjStackTrace *st, size_t len) {
	size_t newSize = st->size;

	while(newSize < st->length + len)
		newSize <<= 1;

	char *newBuf = GCallocate(vm, st->trace, st->size, newSize);
	st->size = newSize;
	st->trace = newBuf;
}

static void stAppenString(BlangVM *vm, ObjStackTrace *st, const char *str) {
	size_t len = strlen(str);
	if(st->length + len >= st->size) 
		growStackTrace(vm, st, len + 1); //the >= and the +1 are for the terminating NUL

	memcpy(&st->trace[st->length], str, len);
	st->length += len;
	st->trace[st->length] = '\0';
}

void stRecordFrame(BlangVM *vm, ObjStackTrace *st, Frame *f, int depth) {
	if(st->lastTracedFrame == depth) return;

	st->lastTracedFrame = depth;

	ObjFunction *fn = f->closure->fn;
	size_t op = f->ip - fn->chunk.code - 1;

	char line[MAX_STRLEN_FOR_INT_TYPE(int) + 1] = { 0 };
	sprintf(line, "%d", getBytecodeSrcLine(&fn->chunk, op));

	stAppenString(vm, st, "[line ");
	stAppenString(vm, st, line);
	stAppenString(vm, st, "] ");

	stAppenString(vm, st, "module ");
	stAppenString(vm, st, fn->module->name->data);
	stAppenString(vm, st, " in ");

	if(fn->name != NULL) {
		stAppenString(vm, st, fn->name->data);
		stAppenString(vm, st, "()\n");
	} else {
		stAppenString(vm, st, "<main>\n");
	}
}

#define LIST_DEF_SZ    8
#define LIST_GROW_RATE 2

ObjList *newList(BlangVM *vm, size_t startSize) {
	size_t size = startSize == 0 ? LIST_DEF_SZ : startSize;
	Value *arr = GC_ALLOC(vm, sizeof(Value) * size);
	ObjList *l = (ObjList*) newObj(vm, sizeof(*l), vm->lstClass, OBJ_LIST);
	l->size  = size;
	l->count = 0;
	l->arr   = arr;
	return l;
}

static void growList(BlangVM *vm, ObjList *lst) {
	size_t newSize = lst->size * LIST_GROW_RATE;
	lst->arr  = GCallocate(vm, lst->arr, sizeof(Value) * lst->size, sizeof(Value) * newSize);
	lst->size = newSize;
}

void listAppend(BlangVM *vm, ObjList *lst, Value val) {
	// if the list get resized a GC may kick in, so push val as root
	push(vm, val);
	if(lst->count + 1 > lst->size) {
		growList(vm, lst);
	}
	lst->arr[lst->count++] = val;
	pop(vm); // pop val
}

void listInsert(BlangVM *vm, ObjList *lst, size_t index, Value val) {
	// if the list get resized a GC may kick in, so push val as root
	push(vm, val);
	if(lst->count + 1 > lst->size) {
		growList(vm, lst);
	}

	Value *arr = lst->arr;
	for(size_t i = lst->count - 1; i >= index; i--) {
		arr[i + 1] = arr[i];
	}
	arr[index] = val;
	lst->count++;
	pop(vm); // pop val
}

void listRemove(BlangVM *vm, ObjList *lst, size_t index) {
	Value *arr = lst->arr;
	for(size_t i = index + 1; i < lst->count; i++) {
		arr[i - 1] = arr[i];
	}
	lst->count--;
}

ObjString *allocateString(BlangVM *vm, size_t length) {
	char *data = GC_ALLOC(vm, length + 1);
	ObjString *str = (ObjString*) newObj(vm, sizeof(*str), vm->strClass, OBJ_STRING);
	str->length = length;
	str->hash = 0;
	str->interned = false;
	str->data = data;
	str->data[str->length] = '\0';
	return str;
}

void reallocateString(BlangVM *vm, ObjString *str, size_t newLen) {
	if(str->hash != 0) {
		fprintf(stderr, "Cannot use reallocateString to reallocate a string already in use by the runtime.\n");
		abort();
	}

	push(vm, OBJ_VAL(str));
	str->data = GCallocate(vm, str->data, str->length + 1, newLen + 1);
	str->length = newLen;
	str->data[str->length] = '\0';
	pop(vm);
}

static ObjString *newString(BlangVM *vm, const char *cstring, size_t length) {
	ObjString *str = allocateString(vm, length);
	memcpy(str->data, cstring, length);
	return str;
}

ObjString *copyString(BlangVM *vm, const char *str, size_t length, bool intern) {
	if(intern) {
		uint32_t hash = hashString(str, length);
		ObjString *interned = HashTableGetString(&vm->strings, str, length, hash);
		if(interned == NULL) {
			interned = newString(vm, str, length);
			interned->hash = hash;
			interned->interned = true;
			hashTablePut(&vm->strings, interned, NULL_VAL);
		}
		return interned;
	}
	return newString(vm, str, length);
}

static void freeObject(BlangVM *vm, Obj *o) {
	switch(o->type) {
	case OBJ_STRING: {
		ObjString *s = (ObjString*) o;
		GC_FREEARRAY(vm, char, s->data, s->length + 1);
		GC_FREE(vm, ObjString, s);
		break;
	}
	case OBJ_NATIVE: {
		ObjNative *n = (ObjNative*) o;
		GC_FREEARRAY(vm, Value, n->defaults, n->defaultc);
		GC_FREE(vm, ObjNative, n);
		break;
	}
	case OBJ_FUNCTION: {
		ObjFunction *f = (ObjFunction*) o;
		freeChunk(&f->chunk);
		GC_FREEARRAY(vm, Value, f->defaults, f->defaultc);
		GC_FREE(vm, ObjFunction, f);
		break;
	}
	case OBJ_CLASS: {
		ObjClass *cls = (ObjClass*) o;
		freeHashTable(&cls->methods);
		GC_FREE(vm, ObjClass, cls);
		break;
	}
	case OBJ_INST: {
		ObjInstance *i = (ObjInstance*) o;
		freeHashTable(&i->fields);
		GC_FREE(vm, ObjInstance, i);
		break;
	}
	case OBJ_MODULE: {
		ObjModule *m = (ObjModule*) o;
		freeHashTable(&m->globals);
		GC_FREE(vm, ObjModule, m);
		break;
	}
	case OBJ_BOUND_METHOD: {
		ObjBoundMethod *b = (ObjBoundMethod*) o;
		GC_FREE(vm, ObjBoundMethod, b);
		break;
	}
	case OBJ_LIST: {
		ObjList *l = (ObjList*) o;
		GC_FREEARRAY(vm, Value, l->arr, l->size);
		GC_FREE(vm, ObjList, l);
		break;
	}
	case OBJ_TUPLE: {
		ObjTuple *t = (ObjTuple*) o;
		GC_FREE_VAR(vm, ObjTuple, Value, t->size, t);
		break;
	}
	case OBJ_STACK_TRACE: {
		ObjStackTrace *st = (ObjStackTrace*) o;
		GC_FREEARRAY(vm, char, st->trace, st->size);
		GC_FREE(vm, ObjStackTrace, st);
		break;
	}
	case OBJ_CLOSURE: {
		ObjClosure *closure = (ObjClosure*) o;
		GC_FREE_VAR(vm, ObjClosure, ObjUpvalue*, closure->upvalueCount, o);
		break;
	}
	case OBJ_UPVALUE: {
		ObjUpvalue *upvalue = (ObjUpvalue*) o;
		GC_FREE(vm, ObjUpvalue, upvalue);
		break;
	}
	}
}

void freeObjects(BlangVM *vm) {
	Obj **head = &vm->objects;
	while(*head != NULL) {
		if(!(*head)->reached)  {
			Obj *u = *head;
			*head = u->next;

#ifdef DBG_PRINT_GC
			printf("GC_FREE: unreached object %p type: %s\n", (void*)u, ObjTypeName[u->type]);
#endif

			freeObject(vm, u);
		} else {
			(*head)->reached = false;
			head = &(*head)->next;
		}
	}
}

void disableGC(BlangVM *vm , bool disable) {
	vm->disableGC = disable;
}

static void growReached(BlangVM *vm) {
	vm->reachedCapacity *= REACHED_GROW_RATE;
	vm->reachedStack = realloc(vm->reachedStack, sizeof(Obj*) * vm->reachedCapacity);
}

static void addReachedObject(BlangVM *vm, Obj *o) {
	if(vm->reachedCount + 1 > vm->reachedCapacity) {
		growReached(vm);
	}
	vm->reachedStack[vm->reachedCount++] = o;
}

void reachObject(BlangVM *vm, Obj *o) {
	if(o == NULL || o->reached) return;

#ifdef DBG_PRINT_GC
	printf("REACHED: Object %p type: %s repr: ", (void*)o, ObjTypeName[o->type]);
	printObj(o);
	printf("\n");
#endif

	o->reached = true;
	addReachedObject(vm, o);
}

void reachValue(BlangVM *vm, Value v) {
	if(IS_OBJ(v)) reachObject(vm, AS_OBJ(v));
}

static void reachValueArray(BlangVM *vm, ValueArray *a) {
	for(int i = 0; i < a->count; i++) {
		reachValue(vm, a->arr[i]);
	}
}

static void reachHashTable(BlangVM *vm, HashTable *t) {
	for(size_t i = 0; i < t->size; i++) {
		Entry *buckHead = t->entries[i];
		while(buckHead != NULL) {
			reachObject(vm, (Obj*) buckHead->key);
			reachValue(vm, buckHead->value);
			buckHead = buckHead->next;
		}
	}
}

static void recursevelyReach(BlangVM *vm, Obj *o) {
#ifdef DBG_PRINT_GC
	printf("Recursevely exploring object %p...\n", (void*)o);
#endif

	reachObject(vm, (Obj*) o->cls);

	switch(o->type) {
	case OBJ_NATIVE: {
		ObjNative *n = (ObjNative*) o;
		reachObject(vm, (Obj*) n->name);
		reachObject(vm, (Obj*) n->module);
		for(uint8_t i = 0; i < n->defaultc; i++) {
			reachValue(vm, n->defaults[i]);
		}
		break;
	}
	case OBJ_FUNCTION: {
		ObjFunction *func = (ObjFunction*) o;
		reachObject(vm, (Obj*) func->name);
		reachObject(vm, (Obj*) func->module);
		reachValueArray(vm, &func->chunk.consts);
		for(uint8_t i = 0; i < func->defaultc; i++) {
			reachValue(vm, func->defaults[i]);
		}
		break;
	}
	case OBJ_CLASS: {
		ObjClass *cls = (ObjClass*) o;
		reachObject(vm, (Obj*) cls->name);
		reachObject(vm, (Obj*) cls->superCls);
		reachHashTable(vm, &cls->methods);
		break;
	}
	case OBJ_INST: {
		ObjInstance *i = (ObjInstance*) o;
		reachHashTable(vm, &i->fields);
		break;
	}
	case OBJ_MODULE: {
		ObjModule *m = (ObjModule*) o;
		reachObject(vm, (Obj*) m->name);
		reachHashTable(vm, &m->globals);
		break;
	}
	case OBJ_LIST: {
		ObjList *l = (ObjList*) o;
		for(size_t i = 0; i < l->count; i++) {
			reachValue(vm, l->arr[i]);
		}
		break;
	}
	case OBJ_TUPLE: {
		ObjTuple *t = (ObjTuple*) o;
		for(size_t i = 0; i < t->size; i++) {
			reachValue(vm, t->arr[i]);
		}
		break;
	}
	case OBJ_BOUND_METHOD: {
		ObjBoundMethod *b = (ObjBoundMethod*) o;
		reachValue(vm, b->bound);
		reachObject(vm, (Obj*) b->method);
		break;
	}
	case OBJ_CLOSURE: {
		ObjClosure *closure = (ObjClosure*) o;
		reachObject(vm, (Obj*) closure->fn);
		for(uint8_t i = 0; i < closure->fn->upvaluec; i++) {
			reachObject(vm, (Obj*) closure->upvalues[i]);
		}
		break;
	}
	case OBJ_UPVALUE: {
		ObjUpvalue *upvalue = (ObjUpvalue*) o;
		reachValue(vm, *upvalue->addr);
		break;
	}
	case OBJ_STRING: break;
	case OBJ_STACK_TRACE: break;
	}
}

void garbageCollect(BlangVM *vm) {
#ifdef DBG_PRINT_GC
	size_t prevAlloc = vm->allocated;
	puts("*--- Starting GC ---*");
#endif

	//init reached object stack
	vm->reachedStack = malloc(sizeof(Obj*) * REACHED_DEFAULT_SZ);
	vm->reachedCapacity = REACHED_DEFAULT_SZ;

	// reach objects in vm
	reachObject(vm, (Obj*) vm->importpaths);

	reachObject(vm, (Obj*) vm->clsClass);
	reachObject(vm, (Obj*) vm->objClass);
	reachObject(vm, (Obj*) vm->strClass);
	reachObject(vm, (Obj*) vm->boolClass);
	reachObject(vm, (Obj*) vm->lstClass);
	reachObject(vm, (Obj*) vm->numClass);
	reachObject(vm, (Obj*) vm->funClass);
	reachObject(vm, (Obj*) vm->modClass);
	reachObject(vm, (Obj*) vm->nullClass);

	reachObject(vm, (Obj*) vm->add);
	reachObject(vm, (Obj*) vm->mul);
	reachObject(vm, (Obj*) vm->div);
	reachObject(vm, (Obj*) vm->mod);
	reachObject(vm, (Obj*) vm->get);
	reachObject(vm, (Obj*) vm->set);

	reachObject(vm, (Obj*) vm->radd);
	reachObject(vm, (Obj*) vm->rsub);
	reachObject(vm, (Obj*) vm->rmul);
	reachObject(vm, (Obj*) vm->rdiv);
	reachObject(vm, (Obj*) vm->rmod);

	reachObject(vm, (Obj*) vm->lt);
	reachObject(vm, (Obj*) vm->le);
	reachObject(vm, (Obj*) vm->gt);
	reachObject(vm, (Obj*) vm->ge);
	reachObject(vm, (Obj*) vm->eq);

	reachObject(vm, (Obj*) vm->neg);

	//reach current exception if present
	reachObject(vm, (Obj*) vm->exception);

	reachObject(vm, (Obj*) vm->ctor);
	reachObject(vm, (Obj*) vm->stField);
	reachObject(vm, (Obj*) vm->emptyTup);

	//reach vm global vars
	reachHashTable(vm, &vm->modules);
	//reach elements on the stack
	for(Value *v = vm->stack; v < vm->sp; v++) {
		reachValue(vm, *v);
	}
	//reach elements on the frame stack
	for(int i = 0; i < vm->frameCount; i++) {
		reachObject(vm, (Obj*) vm->frames[i].closure);
	}
	//reach open upvalues
	for(ObjUpvalue *upvalue = vm->upvalues; upvalue != NULL; upvalue = upvalue->next) {
		reachObject(vm, (Obj*)upvalue);
	}

	//reach the compiler objects
	reachCompilerRoots(vm, vm->currCompiler);

	//recursevely reach objs held by other reached objs
	while(vm->reachedCount != 0) {
		recursevelyReach(vm, vm->reachedStack[--vm->reachedCount]);
	}

	//remove unused strings
	removeUnreachedStrings(&vm->strings);

	//free the garbage
	freeObjects(vm);

	//free the reached objects stack
	free(vm->reachedStack);
	vm->reachedStack = NULL;
	vm->reachedCapacity = 0;
	vm->reachedCount = 0;

	vm->nextGC = vm->allocated * HEAP_GROW_RATE;



#ifdef DBG_PRINT_GC
	size_t curr = prevAlloc - vm->allocated;
	printf("Completed GC, prev allocated: %lu, curr allocated "
	       "%lu, freed: %lu bytes of memory, next GC: %lu.\n",
		   prevAlloc, vm->allocated, curr, vm->nextGC);
	puts("*--- End  of  GC ---*\n");
#endif
}
