#include "gc.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "array.h"
#include "compiler.h"
#include "conf.h"
#include "int_hashtable.h"
#include "object.h"
#include "profiler.h"
#include "value.h"
#include "value_hashtable.h"
#include "vm.h"

void* gcAlloc(JStarVM* vm, void* ptr, size_t oldSz, size_t newSz) {
    vm->allocated += newSz - oldSz;
    if(newSz > oldSz) {
#ifdef JSTAR_DBG_STRESS_GC
        garbageCollect(vm);
#else
        if(vm->allocated > vm->nextGC) {
            garbageCollect(vm);
        }
#endif
    }
    void* mem = vm->realloc(ptr, oldSz, newSz, vm->userData);
    if(newSz != 0) {
        JSR_ASSERT(mem, "Out of memory");
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

void reachObject(JStarVM* vm, Obj* o) {
    if(o == NULL || o->reached) return;

#ifdef JSTAR_DBG_PRINT_GC
    printf("REACHED: Object %p type: %s repr: ", (void*)o, ObjTypeNames[o->type]);
    printObj(o);
    printf("\n");
#endif

    o->reached = true;
    arrayAppend(vm, &vm->reachedStack, o);
}

void reachValue(JStarVM* vm, Value v) {
    if(IS_OBJ(v)) reachObject(vm, AS_OBJ(v));
}

static void reachValues(JStarVM* vm, Values a) {
    for(size_t i = 0; i < a.count; i++) {
        reachValue(vm, a.items[i]);
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
        reachObject(vm, (Obj*)n->proto.name);
        reachObject(vm, (Obj*)n->proto.module);
        for(uint8_t i = 0; i < n->proto.defCount; i++) {
            reachValue(vm, n->proto.defaults[i]);
        }
        break;
    }
    case OBJ_FUNCTION: {
        ObjFunction* func = (ObjFunction*)o;
        reachObject(vm, (Obj*)func->proto.name);
        reachObject(vm, (Obj*)func->proto.module);
        reachValues(vm, func->code.consts);
        for(size_t i = 0; i < func->code.symbols.count; i++) {
            reachObject(vm, (Obj*)func->code.symbols.items[i].cache.key);
        }
        for(uint8_t i = 0; i < func->proto.defCount; i++) {
            reachValue(vm, func->proto.defaults[i]);
        }
        break;
    }
    case OBJ_CLASS: {
        ObjClass* cls = (ObjClass*)o;
        reachObject(vm, (Obj*)cls->name);
        reachObject(vm, (Obj*)cls->superCls);
        reachValueHashTable(vm, &cls->methods);
        reachIntHashTable(vm, &cls->fields);
        break;
    }
    case OBJ_INST: {
        ObjInstance* i = (ObjInstance*)o;
        for(Value* v = i->fields; v < i->fields + i->size; v++) {
            reachValue(vm, *v);
        }
        break;
    }
    case OBJ_MODULE: {
        ObjModule* m = (ObjModule*)o;
        reachObject(vm, (Obj*)m->name);
        reachObject(vm, (Obj*)m->path);
        reachIntHashTable(vm, &m->globalNames);
        for(int i = 0; i < m->globalsCapacity; i++) {
            reachValue(vm, m->globals[i]);
        }
        break;
    }
    case OBJ_LIST: {
        ObjList* l = (ObjList*)o;
        for(size_t i = 0; i < l->count; i++) {
            reachValue(vm, l->items[i]);
        }
        break;
    }
    case OBJ_TUPLE: {
        ObjTuple* t = (ObjTuple*)o;
        for(size_t i = 0; i < t->count; i++) {
            reachValue(vm, t->items[i]);
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
        reachValue(vm, b->receiver);
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
    case OBJ_GENERATOR: {
        ObjGenerator* gen = (ObjGenerator*)o;
        reachObject(vm, (Obj*)gen->closure);
        for(size_t i = 0; i < gen->frame.stackTop; i++) {
            reachValue(vm, gen->savedStack[i]);
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
        for(size_t i = 0; i < stackTrace->records.count; i++) {
            reachObject(vm, (Obj*)stackTrace->records.items[i].funcName);
            reachObject(vm, (Obj*)stackTrace->records.items[i].moduleName);
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

    {
        PROFILE("{reach-objects}::garbageCollect")

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

        reachObject(vm, (Obj*)vm->argv);

        for(int i = 0; i < METH_SIZE; i++) {
            reachObject(vm, (Obj*)vm->specialMethods[i]);
        }

        reachObject(vm, (Obj*)vm->excErr);
        reachObject(vm, (Obj*)vm->excTrace);
        reachObject(vm, (Obj*)vm->excCause);

        reachObject(vm, (Obj*)vm->emptyTup);
        reachValueHashTable(vm, &vm->modules);

        for(Value* v = vm->stack; v < vm->sp; v++) {
            reachValue(vm, *v);
        }

        for(int i = 0; i < vm->frameCount; i++) {
            reachObject(vm, vm->frames[i].fn);
        }

        for(ObjUpvalue* upvalue = vm->upvalues; upvalue != NULL; upvalue = upvalue->next) {
            reachObject(vm, (Obj*)upvalue);
        }

        for(JStarSymbol* s = vm->symbols; s != NULL; s = s->next) {
            reachObject(vm, s->sym.key);
        }

        reachCompilerRoots(vm, vm->currCompiler);
    }

    {
        PROFILE("{recursively-reach}::garbageCollect")

        while(vm->reachedStack.count != 0) {
            recursevelyReach(vm, vm->reachedStack.items[--vm->reachedStack.count]);
        }
    }

    sweepStrings(&vm->stringPool);
    sweepObjects(vm);

    vm->reachedStack.count = 0;
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
