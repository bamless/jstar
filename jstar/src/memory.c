#include "memory.h"
#include "jstar.h"
#include "chunk.h"
#include "compiler.h"
#include "dynload.h"
#include "hashtable.h"
#include "jstar.h"
#include "vm.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define REACHED_DEFAULT_SZ 16
#define REACHED_GROW_RATE 2
#define HEAP_GROW_RATE 2

void *GCallocate(JStarVM *vm, void *ptr, size_t oldsize, size_t size) {
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

static void freeObject(JStarVM *vm, Obj *o) {
    switch(o->type) {
    case OBJ_STRING: {
        ObjString *s = (ObjString *)o;
        GC_FREEARRAY(vm, char, s->data, s->length + 1);
        GC_FREE(vm, ObjString, s);
        break;
    }
    case OBJ_NATIVE: {
        ObjNative *n = (ObjNative *)o;
        GC_FREEARRAY(vm, Value, n->c.defaults, n->c.defaultc);
        GC_FREE(vm, ObjNative, n);
        break;
    }
    case OBJ_FUNCTION: {
        ObjFunction *f = (ObjFunction *)o;
        freeChunk(&f->chunk);
        GC_FREEARRAY(vm, Value, f->c.defaults, f->c.defaultc);
        GC_FREE(vm, ObjFunction, f);
        break;
    }
    case OBJ_CLASS: {
        ObjClass *cls = (ObjClass *)o;
        freeHashTable(&cls->methods);
        GC_FREE(vm, ObjClass, cls);
        break;
    }
    case OBJ_INST: {
        ObjInstance *i = (ObjInstance *)o;
        freeHashTable(&i->fields);
        GC_FREE(vm, ObjInstance, i);
        break;
    }
    case OBJ_MODULE: {
        ObjModule *m = (ObjModule *)o;
        freeHashTable(&m->globals);
        if(m->natives.dynlib) dynfree(m->natives.dynlib);
        GC_FREE(vm, ObjModule, m);
        break;
    }
    case OBJ_BOUND_METHOD: {
        ObjBoundMethod *b = (ObjBoundMethod *)o;
        GC_FREE(vm, ObjBoundMethod, b);
        break;
    }
    case OBJ_LIST: {
        ObjList *l = (ObjList *)o;
        GC_FREEARRAY(vm, Value, l->arr, l->size);
        GC_FREE(vm, ObjList, l);
        break;
    }
    case OBJ_TUPLE: {
        ObjTuple *t = (ObjTuple *)o;
        GC_FREE_VAR(vm, ObjTuple, Value, t->size, t);
        break;
    }
    case OBJ_TABLE: {
        ObjTable *t = (ObjTable *)o;
        if(t->entries != NULL) {
            GC_FREEARRAY(vm, TableEntry, t->entries, t->sizeMask + 1);
        }
        GC_FREE(vm, ObjTable, t);
        break;
    }
    case OBJ_STACK_TRACE: {
        ObjStackTrace *st = (ObjStackTrace *)o;
        jsrBufferFree(&st->stacktrace);
        GC_FREE(vm, ObjStackTrace, st);
        break;
    }
    case OBJ_CLOSURE: {
        ObjClosure *closure = (ObjClosure *)o;
        GC_FREE_VAR(vm, ObjClosure, ObjUpvalue *, closure->upvalueCount, o);
        break;
    }
    case OBJ_UPVALUE: {
        ObjUpvalue *upvalue = (ObjUpvalue *)o;
        GC_FREE(vm, ObjUpvalue, upvalue);
        break;
    }
    }
}

void freeObjects(JStarVM *vm) {
    Obj **head = &vm->objects;
    while(*head != NULL) {
        if(!(*head)->reached) {
            Obj *u = *head;
            *head = u->next;

#ifdef DBG_PRINT_GC
            printf("GC_FREE: unreached object %p type: %s\n", (void *)u, ObjTypeName[u->type]);
#endif

            freeObject(vm, u);
        } else {
            (*head)->reached = false;
            head = &(*head)->next;
        }
    }
}

void disableGC(JStarVM *vm, bool disable) {
    vm->disableGC = disable;
}

static void growReached(JStarVM *vm) {
    vm->reachedCapacity *= REACHED_GROW_RATE;
    vm->reachedStack = realloc(vm->reachedStack, sizeof(Obj *) * vm->reachedCapacity);
}

static void addReachedObject(JStarVM *vm, Obj *o) {
    if(vm->reachedCount + 1 > vm->reachedCapacity) {
        growReached(vm);
    }
    vm->reachedStack[vm->reachedCount++] = o;
}

void reachObject(JStarVM *vm, Obj *o) {
    if(o == NULL || o->reached) return;

#ifdef DBG_PRINT_GC
    printf("REACHED: Object %p type: %s repr: ", (void *)o, ObjTypeName[o->type]);
    printObj(o);
    printf("\n");
#endif

    o->reached = true;
    addReachedObject(vm, o);
}

void reachValue(JStarVM *vm, Value v) {
    if(IS_OBJ(v)) reachObject(vm, AS_OBJ(v));
}

static void reachValueArray(JStarVM *vm, ValueArray *a) {
    for(int i = 0; i < a->count; i++) {
        reachValue(vm, a->arr[i]);
    }
}

static void recursevelyReach(JStarVM *vm, Obj *o) {
#ifdef DBG_PRINT_GC
    printf("Recursevely exploring object %p...\n", (void *)o);
#endif

    reachObject(vm, (Obj *)o->cls);

    switch(o->type) {
    case OBJ_NATIVE: {
        ObjNative *n = (ObjNative *)o;
        reachObject(vm, (Obj *)n->c.name);
        reachObject(vm, (Obj *)n->c.module);
        for(uint8_t i = 0; i < n->c.defaultc; i++) {
            reachValue(vm, n->c.defaults[i]);
        }
        break;
    }
    case OBJ_FUNCTION: {
        ObjFunction *func = (ObjFunction *)o;
        reachObject(vm, (Obj *)func->c.name);
        reachObject(vm, (Obj *)func->c.module);
        reachValueArray(vm, &func->chunk.consts);
        for(uint8_t i = 0; i < func->c.defaultc; i++) {
            reachValue(vm, func->c.defaults[i]);
        }
        break;
    }
    case OBJ_CLASS: {
        ObjClass *cls = (ObjClass *)o;
        reachObject(vm, (Obj *)cls->name);
        reachObject(vm, (Obj *)cls->superCls);
        reachHashTable(vm, &cls->methods);
        break;
    }
    case OBJ_INST: {
        ObjInstance *i = (ObjInstance *)o;
        reachHashTable(vm, &i->fields);
        break;
    }
    case OBJ_MODULE: {
        ObjModule *m = (ObjModule *)o;
        reachObject(vm, (Obj *)m->name);
        reachHashTable(vm, &m->globals);
        break;
    }
    case OBJ_LIST: {
        ObjList *l = (ObjList *)o;
        for(size_t i = 0; i < l->count; i++) {
            reachValue(vm, l->arr[i]);
        }
        break;
    }
    case OBJ_TUPLE: {
        ObjTuple *t = (ObjTuple *)o;
        for(size_t i = 0; i < t->size; i++) {
            reachValue(vm, t->arr[i]);
        }
        break;
    }
    case OBJ_TABLE: {
        ObjTable *t = (ObjTable *)o;
        if(t->entries != NULL) {
            for(size_t i = 0; i < t->sizeMask + 1; i++) {
                reachValue(vm, t->entries[i].key);
                reachValue(vm, t->entries[i].val);
            }
        }
        break;
    }
    case OBJ_BOUND_METHOD: {
        ObjBoundMethod *b = (ObjBoundMethod *)o;
        reachValue(vm, b->bound);
        reachObject(vm, (Obj *)b->method);
        break;
    }
    case OBJ_CLOSURE: {
        ObjClosure *closure = (ObjClosure *)o;
        reachObject(vm, (Obj *)closure->fn);
        for(uint8_t i = 0; i < closure->fn->upvaluec; i++) {
            reachObject(vm, (Obj *)closure->upvalues[i]);
        }
        break;
    }
    case OBJ_UPVALUE: {
        ObjUpvalue *upvalue = (ObjUpvalue *)o;
        reachValue(vm, *upvalue->addr);
        break;
    }
    case OBJ_STRING:
        break;
    case OBJ_STACK_TRACE:
        break;
    }
}

void garbageCollect(JStarVM *vm) {
#ifdef DBG_PRINT_GC
    size_t prevAlloc = vm->allocated;
    puts("*--- Starting GC ---*");
#endif

    // init reached object stack
    vm->reachedStack = malloc(sizeof(Obj *) * REACHED_DEFAULT_SZ);
    vm->reachedCapacity = REACHED_DEFAULT_SZ;

    // reach objects in vm
    reachObject(vm, (Obj *)vm->importpaths);

    reachObject(vm, (Obj *)vm->clsClass);
    reachObject(vm, (Obj *)vm->objClass);
    reachObject(vm, (Obj *)vm->strClass);
    reachObject(vm, (Obj *)vm->boolClass);
    reachObject(vm, (Obj *)vm->lstClass);
    reachObject(vm, (Obj *)vm->numClass);
    reachObject(vm, (Obj *)vm->funClass);
    reachObject(vm, (Obj *)vm->modClass);
    reachObject(vm, (Obj *)vm->nullClass);
    reachObject(vm, (Obj *)vm->stClass);
    reachObject(vm, (Obj *)vm->tupClass);
    reachObject(vm, (Obj *)vm->excClass);
    reachObject(vm, (Obj *)vm->tableClass);

    reachObject(vm, (Obj *)vm->add);
    reachObject(vm, (Obj *)vm->mul);
    reachObject(vm, (Obj *)vm->div);
    reachObject(vm, (Obj *)vm->mod);
    reachObject(vm, (Obj *)vm->get);
    reachObject(vm, (Obj *)vm->set);

    reachObject(vm, (Obj *)vm->radd);
    reachObject(vm, (Obj *)vm->rsub);
    reachObject(vm, (Obj *)vm->rmul);
    reachObject(vm, (Obj *)vm->rdiv);
    reachObject(vm, (Obj *)vm->rmod);

    reachObject(vm, (Obj *)vm->lt);
    reachObject(vm, (Obj *)vm->le);
    reachObject(vm, (Obj *)vm->gt);
    reachObject(vm, (Obj *)vm->ge);
    reachObject(vm, (Obj *)vm->eq);

    reachObject(vm, (Obj *)vm->neg);

    reachObject(vm, (Obj *)vm->ctor);
    reachObject(vm, (Obj *)vm->stField);
    reachObject(vm, (Obj *)vm->emptyTup);

    // reach loaded modules
    reachHashTable(vm, &vm->modules);

    // reach elements on the stack
    for(Value *v = vm->stack; v < vm->sp; v++) {
        reachValue(vm, *v);
    }

    // reach elements on the frame stack
    for(int i = 0; i < vm->frameCount; i++) {
        if(vm->frames[i].fn.type == OBJ_CLOSURE)
            reachObject(vm, (Obj *)vm->frames[i].fn.as.closure);
        else
            reachObject(vm, (Obj *)vm->frames[i].fn.as.native);
    }

    // reach open upvalues
    for(ObjUpvalue *upvalue = vm->upvalues; upvalue != NULL; upvalue = upvalue->next) {
        reachObject(vm, (Obj *)upvalue);
    }

    // reach the compiler objects
    reachCompilerRoots(vm, vm->currCompiler);

    // recursevely reach objs held by other reached objs
    while(vm->reachedCount != 0) {
        recursevelyReach(vm, vm->reachedStack[--vm->reachedCount]);
    }

    // remove unused strings
    removeUnreachedStrings(&vm->strings);

    // free the garbage
    freeObjects(vm);

    // free the reached objects stack
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