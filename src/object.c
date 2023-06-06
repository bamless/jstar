#include "object.h"

#include <stdio.h>
#include <string.h>

#include "gc.h"
#include "util.h"
#include "vm.h"

#define MOD_NAME "__name__"
#define MOD_PATH "__path__"
#define MOD_THIS "__this__"

#define LIST_DEFAULT_CAPACITY 8
#define LIST_GROW_RATE        2

// -----------------------------------------------------------------------------
// OBJECT ALLOCATION FUNCTIONS
// -----------------------------------------------------------------------------

static Obj* newObj(JStarVM* vm, size_t size, ObjClass* cls, ObjType type) {
    Obj* o = GC_ALLOC(vm, size);
    o->cls = cls;
    o->type = type;
    o->reached = false;
    o->next = vm->objects;
    vm->objects = o;
    return o;
}

static Obj* newVarObj(JStarVM* vm, size_t size, size_t varSize, size_t count, ObjClass* cls,
                      ObjType type) {
    return newObj(vm, size + varSize * count, cls, type);
}

static void initProto(Prototype* proto, ObjModule* m, uint8_t args, Value* defaults,
                      uint8_t defCount, bool varg) {
    proto->name = NULL;
    proto->module = m;
    proto->argsCount = args;
    proto->defaults = defaults;
    proto->defCount = defCount;
    proto->vararg = varg;
}

static void zeroValueArray(Value* arr, size_t count) {
    for(size_t i = 0; i < count; i++) {
        arr[i] = NULL_VAL;
    }
}

static Value* allocateDefaultArray(JStarVM* vm, uint8_t defaultCount) {
    if(defaultCount == 0) return NULL;
    Value* defaultArray = GC_ALLOC(vm, sizeof(Value) * defaultCount);
    zeroValueArray(defaultArray, defaultCount);
    return defaultArray;
}

ObjFunction* newFunction(JStarVM* vm, ObjModule* m, uint8_t args, uint8_t defCount, bool varg) {
    Value* defaults = allocateDefaultArray(vm, defCount);
    ObjFunction* fun = (ObjFunction*)newObj(vm, sizeof(*fun), vm->funClass, OBJ_FUNCTION);
    initProto(&fun->proto, m, args, defaults, defCount, varg);
    fun->upvalueCount = 0;
    fun->stackUsage = 0;
    initCode(&fun->code);
    return fun;
}

ObjNative* newNative(JStarVM* vm, ObjModule* m, uint8_t args, uint8_t defCount, bool varg) {
    Value* defaults = allocateDefaultArray(vm, defCount);
    ObjNative* native = (ObjNative*)newObj(vm, sizeof(*native), vm->funClass, OBJ_NATIVE);
    initProto(&native->proto, m, args, defaults, defCount, varg);
    return native;
}

ObjClass* newClass(JStarVM* vm, ObjString* name, ObjClass* superCls) {
    ObjClass* cls = (ObjClass*)newObj(vm, sizeof(*cls), vm->clsClass, OBJ_CLASS);
    cls->name = name;
    cls->superCls = superCls;
    initHashTable(&cls->methods);
    return cls;
}

ObjModule* newModule(JStarVM* vm, const char* path, ObjString* name) {
    ObjModule* mod = (ObjModule*)newObj(vm, sizeof(*mod), vm->modClass, OBJ_MODULE);
    push(vm, OBJ_VAL(mod));

    mod->name = name;
    mod->path = NULL;
    mod->registry = NULL;
    initHashTable(&mod->globals);

    // Implicitly import core
    if(vm->core) {
        hashTableMerge(&mod->globals, &vm->core->globals);
    }

    // Set builtin names for the module object
    mod->path = copyString(vm, path, strlen(path));
    hashTablePut(&mod->globals, copyString(vm, MOD_PATH, strlen(MOD_PATH)), OBJ_VAL(mod->path));
    hashTablePut(&mod->globals, copyString(vm, MOD_NAME, strlen(MOD_NAME)), OBJ_VAL(mod->name));
    hashTablePut(&mod->globals, copyString(vm, MOD_THIS, strlen(MOD_THIS)), OBJ_VAL(mod));
    pop(vm);

    return mod;
}

ObjInstance* newInstance(JStarVM* vm, ObjClass* cls) {
    ObjInstance* inst = (ObjInstance*)newObj(vm, sizeof(*inst), cls, OBJ_INST);
    initHashTable(&inst->fields);
    return inst;
}

ObjClosure* newClosure(JStarVM* vm, ObjFunction* fn) {
    ObjClosure* c = (ObjClosure*)newVarObj(vm, sizeof(*c), sizeof(ObjUpvalue*), fn->upvalueCount,
                                           vm->funClass, OBJ_CLOSURE);
    memset(c->upvalues, 0, sizeof(ObjUpvalue*) * fn->upvalueCount);
    c->upvalueCount = fn->upvalueCount;
    c->fn = fn;
    return c;
}

ObjGenerator* newGenerator(JStarVM* vm, ObjClosure* closure, size_t stackSize) {
    ObjGenerator* gen = (ObjGenerator*)newVarObj(vm, sizeof(*gen), sizeof(Value), stackSize,
                                                 vm->genClass, OBJ_GENERATOR);
    gen->state = GEN_STARTED;
    gen->closure = closure;
    gen->lastYield = NULL_VAL;
    gen->stackSize = stackSize;
    gen->frame.ip = 0;
    gen->frame.handlerCount = 0;
    gen->frame.stackTop = 0;
    return gen;
}

ObjUpvalue* newUpvalue(JStarVM* vm, Value* addr) {
    ObjUpvalue* upvalue = (ObjUpvalue*)newObj(vm, sizeof(*upvalue), NULL, OBJ_UPVALUE);
    upvalue->addr = addr;
    upvalue->closed = NULL_VAL;
    upvalue->next = NULL;
    return upvalue;
}

ObjBoundMethod* newBoundMethod(JStarVM* vm, Value bound, Obj* method) {
    ObjBoundMethod* bm = (ObjBoundMethod*)newObj(vm, sizeof(*bm), vm->funClass, OBJ_BOUND_METHOD);
    bm->receiver = bound;
    bm->method = method;
    return bm;
}

ObjTuple* newTuple(JStarVM* vm, size_t size) {
    if(size == 0 && vm->emptyTup) return vm->emptyTup;
    ObjTuple* tuple = (ObjTuple*)newVarObj(vm, sizeof(*tuple), sizeof(Value), size, vm->tupClass,
                                           OBJ_TUPLE);
    zeroValueArray(tuple->arr, size);
    tuple->size = size;
    return tuple;
}

ObjUserdata* newUserData(JStarVM* vm, size_t size, void (*finalize)(void*)) {
    ObjUserdata* udata = (ObjUserdata*)newVarObj(vm, sizeof(*udata), sizeof(uint8_t), size,
                                                 vm->udataClass, OBJ_USERDATA);
    udata->size = size;
    udata->finalize = finalize;
    return udata;
}

ObjStackTrace* newStackTrace(JStarVM* vm) {
    ObjStackTrace* st = (ObjStackTrace*)newObj(vm, sizeof(*st), vm->stClass, OBJ_STACK_TRACE);
    st->lastTracedFrame = -1;
    st->recordCapacity = 0;
    st->recordSize = 0;
    st->records = NULL;
    return st;
}

ObjList* newList(JStarVM* vm, size_t capacity) {
    Value* arr = NULL;
    if(capacity > 0) arr = GC_ALLOC(vm, sizeof(Value) * capacity);
    ObjList* lst = (ObjList*)newObj(vm, sizeof(*lst), vm->lstClass, OBJ_LIST);
    lst->capacity = capacity;
    lst->size = 0;
    lst->arr = arr;
    return lst;
}

ObjTable* newTable(JStarVM* vm) {
    ObjTable* table = (ObjTable*)newObj(vm, sizeof(*table), vm->tableClass, OBJ_TABLE);
    table->capacityMask = 0;
    table->numEntries = 0;
    table->size = 0;
    table->entries = NULL;
    return table;
}

ObjString* allocateString(JStarVM* vm, size_t length) {
    char* data = GC_ALLOC(vm, length + 1);
    ObjString* str = (ObjString*)newObj(vm, sizeof(*str), vm->strClass, OBJ_STRING);
    str->length = length;
    str->hash = 0;
    str->interned = false;
    str->data = data;
    str->data[str->length] = '\0';
    return str;
}

ObjString* copyString(JStarVM* vm, const char* str, size_t length) {
    uint32_t hash = hashBytes(str, length);
    ObjString* interned = hashTableGetString(&vm->stringPool, str, length, hash);
    if(interned == NULL) {
        interned = allocateString(vm, length);
        memcpy(interned->data, str, length);
        interned->hash = hash;
        interned->interned = true;
        hashTablePut(&vm->stringPool, interned, NULL_VAL);
    }
    return interned;
}

void freeObject(JStarVM* vm, Obj* o) {
    switch(o->type) {
    case OBJ_STRING: {
        ObjString* s = (ObjString*)o;
        GC_FREE_ARRAY(vm, char, s->data, s->length + 1);
        GC_FREE(vm, ObjString, s);
        break;
    }
    case OBJ_NATIVE: {
        ObjNative* n = (ObjNative*)o;
        GC_FREE_ARRAY(vm, Value, n->proto.defaults, n->proto.defCount);
        GC_FREE(vm, ObjNative, n);
        break;
    }
    case OBJ_FUNCTION: {
        ObjFunction* f = (ObjFunction*)o;
        freeCode(&f->code);
        GC_FREE_ARRAY(vm, Value, f->proto.defaults, f->proto.defCount);
        GC_FREE(vm, ObjFunction, f);
        break;
    }
    case OBJ_CLASS: {
        ObjClass* cls = (ObjClass*)o;
        freeHashTable(&cls->methods);
        GC_FREE(vm, ObjClass, cls);
        break;
    }
    case OBJ_INST: {
        ObjInstance* i = (ObjInstance*)o;
        freeHashTable(&i->fields);
        GC_FREE(vm, ObjInstance, i);
        break;
    }
    case OBJ_MODULE: {
        ObjModule* m = (ObjModule*)o;
        freeHashTable(&m->globals);
        GC_FREE(vm, ObjModule, m);
        break;
    }
    case OBJ_BOUND_METHOD: {
        ObjBoundMethod* b = (ObjBoundMethod*)o;
        GC_FREE(vm, ObjBoundMethod, b);
        break;
    }
    case OBJ_LIST: {
        ObjList* l = (ObjList*)o;
        GC_FREE_ARRAY(vm, Value, l->arr, l->capacity);
        GC_FREE(vm, ObjList, l);
        break;
    }
    case OBJ_TUPLE: {
        ObjTuple* t = (ObjTuple*)o;
        GC_FREE_VAR(vm, ObjTuple, Value, t->size, t);
        break;
    }
    case OBJ_TABLE: {
        ObjTable* t = (ObjTable*)o;
        if(t->entries != NULL) {
            GC_FREE_ARRAY(vm, TableEntry, t->entries, t->capacityMask + 1);
        }
        GC_FREE(vm, ObjTable, t);
        break;
    }
    case OBJ_STACK_TRACE: {
        ObjStackTrace* st = (ObjStackTrace*)o;
        if(st->records != NULL) {
            GC_FREE_ARRAY(vm, FrameRecord, st->records, st->recordCapacity);
        }
        GC_FREE(vm, ObjStackTrace, st);
        break;
    }
    case OBJ_CLOSURE: {
        ObjClosure* closure = (ObjClosure*)o;
        GC_FREE_VAR(vm, ObjClosure, ObjUpvalue*, closure->upvalueCount, o);
        break;
    }
    case OBJ_GENERATOR: {
        ObjGenerator* gen = (ObjGenerator*)o;
        GC_FREE_VAR(vm, ObjGenerator, Value, gen->stackSize, o);
        break;
    }
    case OBJ_UPVALUE: {
        ObjUpvalue* upvalue = (ObjUpvalue*)o;
        GC_FREE(vm, ObjUpvalue, upvalue);
        break;
    }
    case OBJ_USERDATA: {
        ObjUserdata* udata = (ObjUserdata*)o;
        if(udata->finalize) udata->finalize((void*)udata->data);
        GC_FREE_VAR(vm, ObjUserdata, uint8_t, udata->size, udata);
        break;
    }
    }
}

// -----------------------------------------------------------------------------
// OBJECT MANIPULATION FUNCTIONS
// -----------------------------------------------------------------------------

static void growList(JStarVM* vm, ObjList* lst) {
    size_t newCap = lst->capacity ? lst->capacity * LIST_GROW_RATE : LIST_DEFAULT_CAPACITY;
    lst->arr = gcAlloc(vm, lst->arr, sizeof(Value) * lst->capacity, sizeof(Value) * newCap);
    lst->capacity = newCap;
}

void listAppend(JStarVM* vm, ObjList* lst, Value val) {
    // if the list get resized a GC may kick in, so push val as root
    if(lst->size + 1 > lst->capacity) {
        push(vm, val);
        growList(vm, lst);
        pop(vm);
    }
    lst->arr[lst->size++] = val;
}

void listInsert(JStarVM* vm, ObjList* lst, size_t index, Value val) {
    // if the list get resized a GC may kick in, so push val as root
    if(lst->size + 1 > lst->capacity) {
        push(vm, val);
        growList(vm, lst);
        pop(vm);
    }

    Value* arr = lst->arr;
    for(size_t i = lst->size; i > index; i--) {
        arr[i] = arr[i - 1];
    }

    arr[index] = val;
    lst->size++;
}

void listRemove(JStarVM* vm, ObjList* lst, size_t index) {
    Value* arr = lst->arr;
    for(size_t i = index + 1; i < lst->size; i++) {
        arr[i - 1] = arr[i];
    }
    lst->size--;
}

uint32_t stringGetHash(ObjString* str) {
    if(str->hash == 0) {
        uint32_t hash = hashBytes(str->data, str->length);
        str->hash = hash ? hash : hash + 1;  // Reserve hash value `0`
    }
    return str->hash;
}

bool stringEquals(ObjString* s1, ObjString* s2) {
    if(s1->interned && s2->interned) return s1 == s2;
    return s1->length == s2->length && memcmp(s1->data, s2->data, s1->length) == 0;
}

void stacktraceDump(JStarVM* vm, ObjStackTrace* st, Frame* f, int depth) {
    if(st->lastTracedFrame == depth) return;
    st->lastTracedFrame = depth;

    if(st->recordSize + 1 >= st->recordCapacity) {
        size_t oldSize = sizeof(FrameRecord) * st->recordCapacity;
        st->recordCapacity = st->records ? st->recordCapacity * 2 : 4;
        st->records = gcAlloc(vm, st->records, oldSize, sizeof(FrameRecord) * st->recordCapacity);
    }

    FrameRecord* record = &st->records[st->recordSize++];
    record->funcName = NULL;
    record->moduleName = NULL;

    switch(f->fn->type) {
    case OBJ_CLOSURE: {
        ObjFunction* fn = ((ObjClosure*)f->fn)->fn;
        Code* code = &fn->code;

        size_t op = f->ip - code->bytecode - 1;
        if(op >= code->size) {
            op = code->size - 1;
        }

        record->line = getBytecodeSrcLine(code, op);
        record->moduleName = fn->proto.module->name;
        record->funcName = fn->proto.name;
        break;
    }
    case OBJ_NATIVE: {
        ObjNative* nat = (ObjNative*)f->fn;
        record->line = -1;
        record->moduleName = nat->proto.module->name;
        record->funcName = nat->proto.name;
        break;
    }
    default:
        UNREACHABLE();
        break;
    }

    if(!record->funcName) {
        record->funcName = copyString(vm, "<main>", 6);
    }
}

Value* getValues(Obj* obj, size_t* size) {
    ASSERT(obj->type == OBJ_LIST || obj->type == OBJ_TUPLE, "Object isn't a Tuple or List.");
    switch(obj->type) {
    case OBJ_LIST: {
        ObjList* lst = (ObjList*)obj;
        *size = lst->size;
        return lst->arr;
    }
    case OBJ_TUPLE: {
        ObjTuple* tup = (ObjTuple*)obj;
        *size = tup->size;
        return tup->arr;
    }
    default:
        UNREACHABLE();
        return *size = 0, NULL;
    }
}

Prototype* getPrototype(Obj* fn) {
    switch(fn->type) {
    case OBJ_CLOSURE:
        return &((ObjClosure*)fn)->fn->proto;
    case OBJ_NATIVE:
        return &((ObjNative*)fn)->proto;
    case OBJ_BOUND_METHOD:
        return getPrototype(((ObjBoundMethod*)fn)->method);
    default:
        UNREACHABLE();
        break;
    }
    return NULL;
}

ObjString* jsrBufferToString(JStarBuffer* b) {
    char* data = gcAlloc(b->vm, b->data, b->capacity, b->size + 1);
    ObjString* s = (ObjString*)newObj(b->vm, sizeof(*s), b->vm->strClass, OBJ_STRING);
    s->interned = false;
    s->length = b->size;
    s->data = data;
    s->hash = 0;
    s->data[s->length] = '\0';
    *b = (JStarBuffer){0};
    return s;
}

JStarBuffer jsrBufferWrap(JStarVM* vm, const void* data, size_t len) {
    return (JStarBuffer){vm, len, len, (char*)data};
}

// -----------------------------------------------------------------------------
// DEBUG FUNCTIONS
// -----------------------------------------------------------------------------

#ifdef JSTAR_DBG_PRINT_GC
const char* ObjTypeNames[] = {
    #define ENUM_STRING(elem) #elem,
    OBJTYPE(ENUM_STRING)
    #undef ENUM_STRING
};
#endif

static void printEscaped(ObjString* s) {
    const char* escaped = "\0\a\b\f\n\r\t\v\\\"";
    const char* unescaped = "0abfnrtv\\\"";
    for(size_t i = 0; i < s->length; i++) {
        int j;
        for(j = 0; j < 10; j++) {
            if(s->data[i] == escaped[j]) {
                printf("\\%c", unescaped[j]);
                break;
            }
        }
        if(j == 10) printf("%c", s->data[i]);
    }
}

void printObj(Obj* o) {
    switch(o->type) {
    case OBJ_STRING:
        printf("\"");
        printEscaped((ObjString*)o);
        printf("\"");
        break;
    case OBJ_FUNCTION: {
        ObjFunction* f = (ObjFunction*)o;
        if(f->proto.module->name->length != 0) {
            printf("<func %s.%s:%d>", f->proto.module->name->data, f->proto.name->data,
                   f->proto.argsCount);
        } else {
            printf("<func %s:%d>", f->proto.name->data, f->proto.argsCount);
        }
        break;
    }
    case OBJ_NATIVE: {
        ObjNative* n = (ObjNative*)o;
        if(n->proto.module->name->length != 0) {
            printf("<native %s.%s:%d>", n->proto.module->name->data, n->proto.name->data,
                   n->proto.argsCount);
        } else {
            printf("<native %s:%d>", n->proto.name->data, n->proto.argsCount);
        }
        break;
    }
    case OBJ_CLASS: {
        ObjClass* cls = (ObjClass*)o;
        printf("<class %s %s>", cls->name->data, cls->superCls ? cls->superCls->name->data : "");
        break;
    }
    case OBJ_INST: {
        ObjInstance* i = (ObjInstance*)o;
        printf("<instance %s>", i->base.cls->name->data);
        break;
    }
    case OBJ_MODULE: {
        ObjModule* m = (ObjModule*)o;
        printf("<module %s>", m->name->data);
        break;
    }
    case OBJ_LIST: {
        ObjList* lst = (ObjList*)o;
        printf("[");
        for(size_t i = 0; i < lst->size; i++) {
            printValue(lst->arr[i]);
            if(i != lst->size - 1) printf(", ");
        }
        printf("]");
        break;
    }
    case OBJ_TUPLE: {
        ObjTuple* t = (ObjTuple*)o;
        printf("(");
        for(size_t i = 0; i < t->size; i++) {
            printValue(t->arr[i]);
            if(i != t->size - 1) printf(", ");
        }
        printf(")");
        break;
    }
    case OBJ_TABLE: {
        ObjTable* t = (ObjTable*)o;
        printf("{");
        if(t->entries != NULL) {
            for(size_t i = 0; i < t->capacityMask + 1; i++) {
                if(!IS_NULL(t->entries[i].key)) {
                    printValue(t->entries[i].key);
                    printf(" : ");
                    printValue(t->entries[i].val);
                    printf(",");
                }
            }
        }
        printf("}");
        break;
    }
    case OBJ_BOUND_METHOD: {
        ObjBoundMethod* b = (ObjBoundMethod*)o;

        char* name;
        if(b->method->type == OBJ_CLOSURE) {
            name = ((ObjClosure*)b->method)->fn->proto.name->data;
        } else {
            name = ((ObjNative*)b->method)->proto.name->data;
        }

        printf("<bound method ");
        printValue(b->receiver);
        printf(":%s>", name);
        break;
    }
    case OBJ_STACK_TRACE:
        printf("<stacktrace %p>", (void*)o);
        break;
    case OBJ_CLOSURE:
        printf("<closure %p>", (void*)o);
        break;
    case OBJ_GENERATOR:
        printf("<generator %p>", (void*)o);
        break;
    case OBJ_UPVALUE:
        printf("<upvalue %p>", (void*)o);
        break;
    case OBJ_USERDATA:
        printf("<userdata %p", (void*)o);
        break;
    }
}
