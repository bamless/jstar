#include "gc.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "code.h"
#include "compiler.h"
#include "hashtable.h"
#include "object.h"
#include "profiler.h"
#include "vm.h"

#define REACHED_DEFAULT_SZ 16
#define REACHED_GROW_RATE  2

void* gcAlloc(JStarVM* vm, void* ptr, size_t oldsize, size_t size) {
    vm->allocated += size - oldsize;
    if(size > oldsize) {
#ifdef JSTAR_DBG_STRESS_GC
        garbageCollect(vm);
#else
        if(vm->allocated > vm->nextGC) {
            garbageCollect(vm);
        }
#endif
    }

    if(size == 0) {
        free(ptr);
        return NULL;
    }

    void* mem = realloc(ptr, size);
    if(!mem) {
        perror("Error");
        abort();
    }

    return mem;
}

void sweepObjects(JStarVM* vm) {
    PROFILE_FUNC()

    Obj** head = &vm->objects;
    while(*head != NULL) {
        if(!(*head)->reached) {
            Obj* u = *head;
            *head = u->next;

#ifdef JSTAR_DBG_PRINT_GC
            printf("GC_FREE: unreached object %p type: %s\n", (void*)u, ObjTypeNames[u->type]);
#endif
            freeObject(vm, u);
        } else {
            (*head)->reached = false;
            head = &(*head)->next;
        }
    }
}

static void growReached(JStarVM* vm) {
    PROFILE_FUNC()
    vm->reachedCapacity *= REACHED_GROW_RATE;
    vm->reachedStack = realloc(vm->reachedStack, sizeof(Obj*) * vm->reachedCapacity);
}

static void addReachedObject(JStarVM* vm, Obj* o) {
    if(vm->reachedCount + 1 > vm->reachedCapacity) {
        growReached(vm);
    }
    vm->reachedStack[vm->reachedCount++] = o;
}

void reachObject(JStarVM* vm, Obj* o) {
    if(o == NULL || o->reached) return;

#ifdef JSTAR_DBG_PRINT_GC
    printf("REACHED: Object %p type: %s repr: ", (void*)o, ObjTypeNames[o->type]);
    printObj(o);
    printf("\n");
#endif

    o->reached = true;
    addReachedObject(vm, o);
}

void reachValue(JStarVM* vm, Value v) {
    if(IS_OBJ(v)) reachObject(vm, AS_OBJ(v));
}

static void reachValueArray(JStarVM* vm, ValueArray* a) {
    for(int i = 0; i < a->size; i++) {
        reachValue(vm, a->arr[i]);
    }
}

static void recursevelyReach(JStarVM* vm, Obj* o) {
#ifdef JSTAR_DBG_PRINT_GC
    printf("Recursevely exploring object %p...\n", (void*)o);
#endif

    reachObject(vm, (Obj*)o->cls);

    switch(o->type) {
    case OBJ_NATIVE: {
        ObjNative* n = (ObjNative*)o;
        reachObject(vm, (Obj*)n->c.name);
        reachObject(vm, (Obj*)n->c.module);
        for(uint8_t i = 0; i < n->c.defCount; i++) {
            reachValue(vm, n->c.defaults[i]);
        }
        break;
    }
    case OBJ_FUNCTION: {
        ObjFunction* func = (ObjFunction*)o;
        reachObject(vm, (Obj*)func->c.name);
        reachObject(vm, (Obj*)func->c.module);
        reachValueArray(vm, &func->code.consts);
        for(uint8_t i = 0; i < func->c.defCount; i++) {
            reachValue(vm, func->c.defaults[i]);
        }
        break;
    }
    case OBJ_CLASS: {
        ObjClass* cls = (ObjClass*)o;
        reachObject(vm, (Obj*)cls->name);
        reachObject(vm, (Obj*)cls->superCls);
        reachHashTable(vm, &cls->methods);
        break;
    }
    case OBJ_INST: {
        ObjInstance* i = (ObjInstance*)o;
        reachHashTable(vm, &i->fields);
        break;
    }
    case OBJ_MODULE: {
        ObjModule* m = (ObjModule*)o;
        reachObject(vm, (Obj*)m->name);
        reachObject(vm, (Obj*)m->path);
        reachHashTable(vm, &m->globals);
        break;
    }
    case OBJ_LIST: {
        ObjList* l = (ObjList*)o;
        for(size_t i = 0; i < l->size; i++) {
            reachValue(vm, l->arr[i]);
        }
        break;
    }
    case OBJ_TUPLE: {
        ObjTuple* t = (ObjTuple*)o;
        for(size_t i = 0; i < t->size; i++) {
            reachValue(vm, t->arr[i]);
        }
        break;
    }
    case OBJ_TABLE: {
        ObjTable* t = (ObjTable*)o;
        if(t->entries != NULL) {
            for(size_t i = 0; i < t->capacityMask + 1; i++) {
                reachValue(vm, t->entries[i].key);
                reachValue(vm, t->entries[i].val);
            }
        }
        break;
    }
    case OBJ_BOUND_METHOD: {
        ObjBoundMethod* b = (ObjBoundMethod*)o;
        reachValue(vm, b->bound);
        reachObject(vm, (Obj*)b->method);
        break;
    }
    case OBJ_CLOSURE: {
        ObjClosure* closure = (ObjClosure*)o;
        reachObject(vm, (Obj*)closure->fn);
        for(uint8_t i = 0; i < closure->upvalueCount; i++) {
            reachObject(vm, (Obj*)closure->upvalues[i]);
        }
        break;
    }
    case OBJ_UPVALUE: {
        ObjUpvalue* upvalue = (ObjUpvalue*)o;
        reachValue(vm, *upvalue->addr);
        break;
    }
    case OBJ_STACK_TRACE: {
        ObjStackTrace* stackTrace = (ObjStackTrace*)o;
        for(int i = 0; i < stackTrace->recordSize; i++) {
            reachObject(vm, (Obj*)stackTrace->records[i].funcName);
            reachObject(vm, (Obj*)stackTrace->records[i].moduleName);
        }
        break;
    }
    case OBJ_USERDATA:
    case OBJ_STRING:
        break;
    }
}

void garbageCollect(JStarVM* vm) {
    PROFILE_FUNC()

#ifdef JSTAR_DBG_PRINT_GC
    size_t prevAlloc = vm->allocated;
    puts("*--- Starting GC ---*");
#endif

    // init reached object stack
    vm->reachedStack = malloc(sizeof(Obj*) * REACHED_DEFAULT_SZ);
    vm->reachedCapacity = REACHED_DEFAULT_SZ;

    {
        PROFILE("{reach-objects}::garbageCollect")

        // reach import paths list
        reachObject(vm, (Obj*)vm->importPaths);

        // reach builtin classes
        reachObject(vm, (Obj*)vm->clsClass);
        reachObject(vm, (Obj*)vm->objClass);
        reachObject(vm, (Obj*)vm->strClass);
        reachObject(vm, (Obj*)vm->boolClass);
        reachObject(vm, (Obj*)vm->lstClass);
        reachObject(vm, (Obj*)vm->numClass);
        reachObject(vm, (Obj*)vm->funClass);
        reachObject(vm, (Obj*)vm->modClass);
        reachObject(vm, (Obj*)vm->nullClass);
        reachObject(vm, (Obj*)vm->stClass);
        reachObject(vm, (Obj*)vm->tupClass);
        reachObject(vm, (Obj*)vm->excClass);
        reachObject(vm, (Obj*)vm->tableClass);
        reachObject(vm, (Obj*)vm->udataClass);

        // reach script argument llist
        reachObject(vm, (Obj*)vm->argv);

        for(int i = 0; i < SYM_END; i++) {
            reachObject(vm, (Obj*)vm->methodSyms[i]);
        }

        // reach empty Tuple singleton
        reachObject(vm, (Obj*)vm->emptyTup);

        // reach loaded modules
        reachHashTable(vm, &vm->modules);

        // reach elements on the stack
        for(Value* v = vm->stack; v < vm->sp; v++) {
            reachValue(vm, *v);
        }

        // reach elements on the frame stack
        for(int i = 0; i < vm->frameCount; i++) {
            reachObject(vm, vm->frames[i].fn);
        }

        // reach open upvalues
        for(ObjUpvalue* upvalue = vm->upvalues; upvalue != NULL; upvalue = upvalue->next) {
            reachObject(vm, (Obj*)upvalue);
        }

        // reach the compiler objects
        reachCompilerRoots(vm, vm->currCompiler);
    }

    {
        PROFILE("{recursively-reach}::garbageCollect")

        // recursevely reach objects held by other reached objects
        while(vm->reachedCount != 0) {
            recursevelyReach(vm, vm->reachedStack[--vm->reachedCount]);
        }
    }

    // free unreached objects
    sweepStrings(&vm->stringPool);
    sweepObjects(vm);

    // free the reached objects stack
    free(vm->reachedStack);
    vm->reachedStack = NULL;
    vm->reachedCapacity = 0;
    vm->reachedCount = 0;

    vm->nextGC = vm->allocated * vm->heapGrowRate;

#ifdef JSTAR_DBG_PRINT_GC
    size_t curr = prevAlloc - vm->allocated;
    printf(
        "Completed GC, prev allocated: %lu, curr allocated "
        "%lu, freed: %lu bytes of memory, next GC: %lu.\n",
        prevAlloc, vm->allocated, curr, vm->nextGC);
    printf("*--- End  of  GC ---*\n");
#endif
}