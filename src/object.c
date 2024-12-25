#include "object.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "conf.h"
#include "field_index.h"
#include "gc.h"
#include "hashtable.h"
#include "util.h"
#include "value.h"
#include "vm.h"

#define LIST_DEFAULT_CAPACITY 8
#define LIST_GROW_RATE        2
#define ST_DEFAULT_CAPACITY   2
#define ST_GROW_RATE          2

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
    cls->fieldCount = 0;
    initFieldIndex(&cls->fields);
    initHashTable(&cls->methods);
    return cls;
}

static void mergeModules(JStarVM* vm, ObjModule* dst, ObjModule* src) {
    fieldIndexMerge(&dst->globalNames, &src->globalNames);
    for(int i = 0; i < src->globalsCount; i++) {
        setGlobalOffset(vm, dst, i, src->globals[i]);
    }
    dst->globalsCount = src->globalsCount;
}

ObjModule* newModule(JStarVM* vm, const char* path, ObjString* name) {
    ObjModule* mod = (ObjModule*)newObj(vm, sizeof(*mod), vm->modClass, OBJ_MODULE);
    push(vm, OBJ_VAL(mod));

    mod->name = name;
    mod->path = NULL;
    mod->registry = NULL;
    mod->globalsCount = 0;
    mod->globalsCapacity = 0;
    mod->globals = NULL;
    initFieldIndex(&mod->globalNames);

    // Implicitly import core
    if(vm->core) {
        mergeModules(vm, mod, vm->core);
    }

    // Set builtin names for the module object
    mod->path = copyString(vm, path, strlen(path));
    setGlobal(vm, mod, copyString(vm, MOD_PATH, strlen(MOD_PATH)), OBJ_VAL(mod->path));
    setGlobal(vm, mod, copyString(vm, MOD_NAME, strlen(MOD_NAME)), OBJ_VAL(mod->name));
    setGlobal(vm, mod, copyString(vm, MOD_THIS, strlen(MOD_THIS)), OBJ_VAL(mod));
    pop(vm);

    return mod;
}

ObjInstance* newInstance(JStarVM* vm, ObjClass* cls) {
    ObjInstance* inst = (ObjInstance*)newObj(vm, sizeof(*inst), cls, OBJ_INST);
    inst->capacity = 0;
    inst->fields = NULL;
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
        freeFieldIndex(&cls->fields);
        freeHashTable(&cls->methods);
        GC_FREE(vm, ObjClass, cls);
        break;
    }
    case OBJ_INST: {
        ObjInstance* i = (ObjInstance*)o;
        GC_FREE_ARRAY(vm, Value, i->fields, i->capacity);
        GC_FREE(vm, ObjInstance, i);
        break;
    }
    case OBJ_MODULE: {
        ObjModule* m = (ObjModule*)o;
        freeFieldIndex(&m->globalNames);
        GC_FREE_ARRAY(vm, Value, m->globals, m->globalsCapacity);
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
        if(udata->finalize) {
            udata->finalize((void*)udata->data);
        }
        GC_FREE_VAR(vm, ObjUserdata, uint8_t, udata->size, udata);
        break;
    }
    }
}

// -----------------------------------------------------------------------------
// OBJECT MANIPULATION FUNCTIONS
// -----------------------------------------------------------------------------

bool getFieldOffset(ObjInstance* inst, int offset, Value* out) {
    if((size_t)offset >= inst->capacity) return false;
    *out = inst->fields[offset];
    return true;
}

void setFieldOffset(JStarVM* vm, ObjInstance* inst, int offset, Value val) {
    if((size_t)offset >= inst->capacity) {
        size_t oldCap = inst->capacity;
        size_t newCap = oldCap ? oldCap : 8;
        while((size_t)offset >= newCap) {
            newCap *= 2;
        }
        Value* newFields = gcAlloc(vm, inst->fields, sizeof(Value) * oldCap,
                                   sizeof(Value) * newCap);
        for(size_t i = oldCap; i < newCap; i++) {
            newFields[i] = NULL_VAL;
        }
        inst->fields = newFields;
        inst->capacity = newCap;
    }
    inst->fields[offset] = val;
}

int setField(JStarVM* vm, ObjClass* cls, ObjInstance* inst, ObjString* key, Value val) {
    int field;
    if(fieldIndexGet(&cls->fields, key, &field)) {
        push(vm, val);
        setFieldOffset(vm, inst, (int)field, val);
        pop(vm);
        return (int)field;
    } else {
        int field = cls->fieldCount++;
        fieldIndexPut(&cls->fields, key, field);
        push(vm, val);
        setFieldOffset(vm, inst, field, val);
        pop(vm);
        return field;
    }
}

bool getField(JStarVM* vm, ObjClass* cls, ObjInstance* inst, ObjString* key, Value* val) {
    int field;
    if(!fieldIndexGet(&cls->fields, key, &field)) return false;
    getFieldOffset(inst, field, val);
    return true;
}

int getFieldIdx(JStarVM* vm, ObjClass* cls, ObjInstance* inst, ObjString* key) {
    int field;
    if(!fieldIndexGet(&cls->fields, key, &field)) return -1;
    return (size_t)field >= inst->capacity ? -1 : field;
}

bool getGlobalOffset(ObjModule* mod, int offset, Value* val) {
    if(offset >= mod->globalsCount) return false;
    *val = mod->globals[offset];
    return true;
}

void setGlobalOffset(JStarVM* vm, ObjModule* mod, int offset, Value val) {
    if(offset >= mod->globalsCapacity) {
        size_t oldCap = mod->globalsCapacity;
        size_t newCap = oldCap ? oldCap : 8;
        while((size_t)offset >= newCap) {
            newCap *= 2;
        }
        Value* newGlobals = gcAlloc(vm, mod->globals, sizeof(Value) * oldCap,
                                    sizeof(Value) * newCap);
        for(size_t i = oldCap; i < newCap; i++) {
            newGlobals[i] = NULL_VAL;
        }
        mod->globals = newGlobals;
        mod->globalsCapacity = newCap;
        if(offset >= mod->globalsCount) {
            mod->globalsCount = offset + 1;
        }
    }
    mod->globals[offset] = val;
}

int setGlobal(JStarVM* vm, ObjModule* mod, ObjString* key, Value val) {
    int global;
    if(fieldIndexGet(&mod->globalNames, key, &global)) {
        push(vm, val);
        setGlobalOffset(vm, mod, (int)global, val);
        pop(vm);
        return (int)global;
    } else {
        int global = mod->globalsCount++;
        fieldIndexPut(&mod->globalNames, key, global);
        push(vm, val);
        setGlobalOffset(vm, mod, global, val);
        pop(vm);
        return global;
    }
}

bool getGlobal(JStarVM* vm, ObjModule* mod, ObjString* key, Value* val) {
    int global;
    if(!fieldIndexGet(&mod->globalNames, key, &global)) return false;
    return getGlobalOffset(mod, (int)global, val);
}

int getGlobalIdx(JStarVM* vm, ObjModule* mod, ObjString* key) {
    int global;
    if(!fieldIndexGet(&mod->globalNames, key, &global)) return -1;
    return global >= mod->globalsCount ? -1 : global;
}

static void growList(JStarVM* vm, ObjList* lst) {
    size_t oldSize = sizeof(Value) * lst->capacity;
    lst->capacity = lst->capacity ? lst->capacity * LIST_GROW_RATE : LIST_DEFAULT_CAPACITY;
    lst->arr = gcAlloc(vm, lst->arr, oldSize, sizeof(Value) * lst->capacity);
}

static void ensureListCapacity(JStarVM* vm, ObjList* lst, Value val) {
    if(lst->size + 1 > lst->capacity) {
        // A GC may kick in, so push val as root
        push(vm, val);
        growList(vm, lst);
        pop(vm);
    }
}

void listAppend(JStarVM* vm, ObjList* lst, Value val) {
    ensureListCapacity(vm, lst, val);
    lst->arr[lst->size++] = val;
}

void listInsert(JStarVM* vm, ObjList* lst, size_t index, Value val) {
    ensureListCapacity(vm, lst, val);

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

static void growFrameRecord(JStarVM* vm, ObjStackTrace* st) {
    size_t oldSize = sizeof(FrameRecord) * st->recordCapacity;
    st->recordCapacity = st->records ? st->recordCapacity * ST_GROW_RATE : ST_DEFAULT_CAPACITY;
    st->records = gcAlloc(vm, st->records, oldSize, sizeof(FrameRecord) * st->recordCapacity);
}

static void ensureFrameRecordCapacity(JStarVM* vm, ObjStackTrace* st) {
    if(st->recordSize + 1 > st->recordCapacity) {
        growFrameRecord(vm, st);
    }
}

void stacktraceDump(JStarVM* vm, ObjStackTrace* st, Frame* f, int depth) {
    if(st->lastTracedFrame == depth) return;
    st->lastTracedFrame = depth;

    ensureFrameRecordCapacity(vm, st);
    FrameRecord* record = &st->records[st->recordSize++];
    record->funcName = NULL;
    record->moduleName = NULL;

    switch(f->fn->type) {
    case OBJ_CLOSURE: {
        ObjClosure* closure = (ObjClosure*)f->fn;
        ObjFunction* fn = closure->fn;
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
        JSR_UNREACHABLE();
        break;
    }

    if(!record->funcName) {
        record->funcName = copyString(vm, "<main>", 6);
    }
}

Value* getValues(Obj* obj, size_t* size) {
    JSR_ASSERT(obj->type == OBJ_LIST || obj->type == OBJ_TUPLE, "Object isn't a Tuple or List.");
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
        JSR_UNREACHABLE();
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
        JSR_UNREACHABLE();
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
    const int len = strlen(escaped);
    for(size_t i = 0; i < s->length; i++) {
        int j;
        for(j = 0; j < len; j++) {
            if(s->data[i] == escaped[j]) {
                printf("\\%c", unescaped[j]);
                break;
            }
        }
        if(j == len) printf("%c", s->data[i]);
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
