#include "object.h"

#include <stdio.h>
#include <string.h>

#include "array.h"
#include "conf.h"
#include "gc.h"
#include "int_hashtable.h"
#include "jstar.h"
#include "util.h"
#include "value.h"
#include "value_hashtable.h"
#include "vm.h"

#ifdef JSTAR_DBG_PRINT_GC
const char* ObjTypeNames[] = {
    #define ENUM_STRING(elem) #elem,
    OBJTYPE(ENUM_STRING)
    #undef ENUM_STRING
};
#endif

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

static void initFunctionBase(FunctionBase* c, ObjModule* m, ObjString* name, uint8_t args,
                             Value* defaults, uint8_t defCount, bool vararg) {
    c->module = m;
    c->name = name;
    c->argsCount = args;
    c->defaults = defaults;
    c->defCount = defCount;
    c->vararg = vararg;
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

ObjFunction* newFunction(JStarVM* vm, ObjModule* m, ObjString* name, uint8_t args, uint8_t defCount,
                         bool vararg) {
    // A GC may kick in on `newObj`, make the name available as a root
    push(vm, OBJ_VAL(name));
    Value* defaults = allocateDefaultArray(vm, defCount);
    ObjClass* fnClass = vm->coreClasses[CORE_CLASS_FUNCTION];
    ObjFunction* fun = (ObjFunction*)newObj(vm, sizeof(*fun), fnClass, OBJ_FUNCTION);
    initFunctionBase(&fun->base, m, name, args, defaults, defCount, vararg);
    fun->upvalueCount = 0;
    fun->stackUsage = 0;
    initCode(&fun->code);
    pop(vm);
    return fun;
}

ObjNative* newNative(JStarVM* vm, ObjModule* m, ObjString* name, uint8_t args, uint8_t defCount,
                     bool vararg, JStarNative fn) {
    // A GC may kick in on `newObj`, make the name available as a root
    push(vm, OBJ_VAL(name));
    Value* defaults = allocateDefaultArray(vm, defCount);
    ObjClass* fnClass = vm->coreClasses[CORE_CLASS_FUNCTION];
    ObjNative* native = (ObjNative*)newObj(vm, sizeof(*native), fnClass, OBJ_NATIVE);
    native->fn = fn;
    initFunctionBase(&native->base, m, name, args, defaults, defCount, vararg);
    pop(vm);
    return native;
}

ObjClass* newClass(JStarVM* vm, ObjString* name, ObjClass* superCls) {
    ObjClass* clsClass = vm->coreClasses[CORE_CLASS_CLASS];
    ObjClass* cls = (ObjClass*)newObj(vm, sizeof(*cls), clsClass, OBJ_CLASS);
    cls->name = name;
    cls->superCls = superCls;
    cls->fieldCount = 0;
    initIntHashTable(vm, &cls->fields);
    initValueHashTable(vm, &cls->methods);
    return cls;
}

static void mergeModules(JStarVM* vm, ObjModule* dst, ObjModule* src) {
    hashTableIntMerge(&dst->globalNames, &src->globalNames);
    for(int i = 0; i < src->globalsCount; i++) {
        moduleSetGlobalAtOffset(vm, dst, i, src->globals[i]);
    }
    dst->globalsCount = src->globalsCount;
}

ObjModule* newModule(JStarVM* vm, const char* path, ObjString* name) {
    ObjClass* modClass = vm->coreClasses[CORE_CLASS_MODULE];
    ObjModule* mod = (ObjModule*)newObj(vm, sizeof(*mod), modClass, OBJ_MODULE);

    // A GC may kick in on hashtable initialization or path allocation,
    // make the module available as a root
    push(vm, OBJ_VAL(mod));

    mod->name = name;
    mod->path = NULL;
    mod->registry = NULL;
    mod->globalsCount = 0;
    mod->globalsCapacity = 0;
    mod->globals = NULL;
    initIntHashTable(vm, &mod->globalNames);

    // Implicitly import core
    if(vm->core) {
        mergeModules(vm, mod, vm->core);
    }

    // Set special global variables for the module object
    mod->path = copyCStringInterned(vm, path);
    moduleSetGlobal(vm, mod, copyCStringInterned(vm, MOD_PATH), OBJ_VAL(mod->path));
    moduleSetGlobal(vm, mod, copyCStringInterned(vm, MOD_NAME), OBJ_VAL(mod->name));
    moduleSetGlobal(vm, mod, copyCStringInterned(vm, MOD_THIS), OBJ_VAL(mod));

    pop(vm);
    return mod;
}

ObjInstance* newInstance(JStarVM* vm, ObjClass* cls) {
    ObjInstance* inst = (ObjInstance*)newObj(vm, sizeof(*inst), cls, OBJ_INST);
    inst->size = 0;
    inst->fields = NULL;
    return inst;
}

ObjClosure* newClosure(JStarVM* vm, ObjFunction* fn) {
    ObjClass* fnClass = vm->coreClasses[CORE_CLASS_FUNCTION];
    ObjClosure* c = (ObjClosure*)newVarObj(vm, sizeof(*c), sizeof(ObjUpvalue*), fn->upvalueCount,
                                           fnClass, OBJ_CLOSURE);
    memset(c->upvalues, 0, sizeof(ObjUpvalue*) * fn->upvalueCount);
    c->upvalueCount = fn->upvalueCount;
    c->fn = fn;
    return c;
}

ObjGenerator* newGenerator(JStarVM* vm, ObjClosure* closure, size_t stackSize) {
    ObjClass* genClass = vm->coreClasses[CORE_CLASS_GENERATOR];
    ObjGenerator* gen = (ObjGenerator*)newVarObj(vm, sizeof(*gen), sizeof(Value), stackSize,
                                                 genClass, OBJ_GENERATOR);
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
    ObjClass* fnClass = vm->coreClasses[CORE_CLASS_FUNCTION];
    ObjBoundMethod* bm = (ObjBoundMethod*)newObj(vm, sizeof(*bm), fnClass, OBJ_BOUND_METHOD);
    bm->receiver = bound;
    bm->method = method;
    return bm;
}

ObjTuple* newTuple(JStarVM* vm, size_t size) {
    if(size == 0 && vm->emptyTup) return vm->emptyTup;
    ObjClass* tupClass = vm->coreClasses[CORE_CLASS_TUPLE];
    ObjTuple* tuple = (ObjTuple*)newVarObj(vm, sizeof(*tuple), sizeof(Value), size, tupClass,
                                           OBJ_TUPLE);
    zeroValueArray(tuple->items, size);
    tuple->count = size;
    return tuple;
}

ObjUserdata* newUserData(JStarVM* vm, size_t size, void (*finalize)(void*)) {
    ObjClass* udataClass = vm->coreClasses[CORE_CLASS_USERDATA];
    ObjUserdata* udata = (ObjUserdata*)newVarObj(vm, sizeof(*udata), sizeof(uint8_t), size,
                                                 udataClass, OBJ_USERDATA);
    udata->size = size;
    udata->finalize = finalize;
    return udata;
}

ObjStackTrace* newStackTrace(JStarVM* vm) {
    ObjClass* stClass = vm->coreClasses[CORE_CLASS_STACKTRACE];
    ObjStackTrace* st = (ObjStackTrace*)newObj(vm, sizeof(*st), stClass, OBJ_STACK_TRACE);
    st->records.items = NULL;
    st->records.capacity = 0;
    st->records.count = 0;
    return st;
}

ObjList* newList(JStarVM* vm, size_t capacity) {
    Value* arr = NULL;
    if(capacity > 0) arr = GC_ALLOC(vm, sizeof(Value) * capacity);
    ObjClass* lstClass = vm->coreClasses[CORE_CLASS_LIST];
    ObjList* lst = (ObjList*)newObj(vm, sizeof(*lst), lstClass, OBJ_LIST);
    lst->capacity = capacity;
    lst->count = 0;
    lst->items = arr;
    return lst;
}

ObjTable* newTable(JStarVM* vm) {
    ObjClass* tableClass = vm->coreClasses[CORE_CLASS_TABLE];
    ObjTable* table = (ObjTable*)newObj(vm, sizeof(*table), tableClass, OBJ_TABLE);
    table->sizeMask = 0;
    table->count = 0;
    table->tombstones = 0;
    table->entries = NULL;
    return table;
}

ObjString* newString(JStarVM* vm, size_t length) {
    char* data = GC_ALLOC(vm, length + 1);
    ObjClass* strClass = vm->coreClasses[CORE_CLASS_STR];
    ObjString* str = (ObjString*)newObj(vm, sizeof(*str), strClass, OBJ_STRING);
    str->length = length;
    str->hash = 0;
    str->interned = false;
    str->data = data;
    str->data[str->length] = '\0';
    return str;
}

ObjString* copyStringInterned(JStarVM* vm, const void* data, size_t length) {
    uint32_t hash = hashBytes(data, length);
    ObjString* interned = hashTableValueGetString(&vm->stringPool, data, length, hash);
    if(interned == NULL) {
        interned = newString(vm, length);
        memcpy(interned->data, data, length);
        interned->hash = hash;
        interned->interned = true;
        hashTableValuePut(&vm->stringPool, interned, NULL_VAL);
    }
    return interned;
}

ObjString* copyCStringInterned(JStarVM* vm, const char* str) {
    return copyStringInterned(vm, str, strlen(str));
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
        GC_FREE_ARRAY(vm, Value, n->base.defaults, n->base.defCount);
        GC_FREE(vm, ObjNative, n);
        break;
    }
    case OBJ_FUNCTION: {
        ObjFunction* f = (ObjFunction*)o;
        freeCode(vm, &f->code);
        GC_FREE_ARRAY(vm, Value, f->base.defaults, f->base.defCount);
        GC_FREE(vm, ObjFunction, f);
        break;
    }
    case OBJ_CLASS: {
        ObjClass* cls = (ObjClass*)o;
        freeIntHashTable(&cls->fields);
        freeValueHashTable(&cls->methods);
        GC_FREE(vm, ObjClass, cls);
        break;
    }
    case OBJ_INST: {
        ObjInstance* i = (ObjInstance*)o;
        GC_FREE_ARRAY(vm, Value, i->fields, i->size);
        GC_FREE(vm, ObjInstance, i);
        break;
    }
    case OBJ_MODULE: {
        ObjModule* m = (ObjModule*)o;
        freeIntHashTable(&m->globalNames);
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
        GC_FREE_ARRAY(vm, Value, l->items, l->capacity);
        GC_FREE(vm, ObjList, l);
        break;
    }
    case OBJ_TUPLE: {
        ObjTuple* t = (ObjTuple*)o;
        GC_FREE_VAR(vm, ObjTuple, Value, t->count, t);
        break;
    }
    case OBJ_TABLE: {
        ObjTable* t = (ObjTable*)o;
        if(t->entries != NULL) {
            GC_FREE_ARRAY(vm, TableEntry, t->entries, t->sizeMask + 1);
        }
        GC_FREE(vm, ObjTable, t);
        break;
    }
    case OBJ_STACK_TRACE: {
        ObjStackTrace* st = (ObjStackTrace*)o;
        arrayFreeGC(vm, &st->records);
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

#define ENSURE_VALUES(vm, off, arr, size)                                         \
    if((size_t)off >= (size_t)size) {                                             \
        size_t oldSize = size;                                                    \
        size_t newSize = oldSize ? oldSize : 8;                                   \
        while((size_t)offset >= newSize) newSize *= 2;                            \
        arr = gcAlloc(vm, arr, sizeof(Value) * oldSize, sizeof(Value) * newSize); \
        for(size_t i = oldSize; i < newSize; i++) {                               \
            arr[i] = NULL_VAL;                                                    \
        }                                                                         \
        size = newSize;                                                           \
    }

bool instanceGetFieldAtOffset(ObjInstance* inst, int offset, Value* out) {
    if((size_t)offset >= inst->size) return false;
    *out = inst->fields[offset];
    return true;
}

void instanceSetFieldAtOffset(JStarVM* vm, ObjInstance* inst, int offset, Value val) {
    ENSURE_VALUES(vm, offset, inst->fields, inst->size);
    inst->fields[offset] = val;
}

int instanceSetField(JStarVM* vm, ObjClass* cls, ObjInstance* inst, ObjString* key, Value val) {
    int offset;
    if(hashTableIntGet(&cls->fields, key, &offset)) {
        push(vm, val);
        instanceSetFieldAtOffset(vm, inst, offset, val);
        pop(vm);
        return offset;
    } else {
        int offset = cls->fieldCount++;
        hashTableIntPut(&cls->fields, key, offset);
        push(vm, val);
        instanceSetFieldAtOffset(vm, inst, offset, val);
        pop(vm);
        return offset;
    }
}

bool instanceGetField(ObjClass* cls, ObjInstance* inst, ObjString* key, Value* out) {
    int offset = instanceGetFieldOffset(cls, inst, key);
    if(offset == -1) return false;
    *out = inst->fields[offset];
    return true;
}

int instanceGetFieldOffset(ObjClass* cls, ObjInstance* inst, ObjString* key) {
    int offset;
    if(!hashTableIntGet(&cls->fields, key, &offset)) return -1;
    return (size_t)offset >= inst->size ? -1 : offset;
}

void moduleGetGlobalAtOffset(ObjModule* mod, int offset, Value* out) {
    JSR_ASSERT(offset < mod->globalsCount, "Global offset out of bounds");
    *out = mod->globals[offset];
}

void moduleSetGlobalAtOffset(JStarVM* vm, ObjModule* mod, int offset, Value val) {
    ENSURE_VALUES(vm, offset, mod->globals, mod->globalsCapacity);
    if(offset >= mod->globalsCount) mod->globalsCount = offset + 1;
    mod->globals[offset] = val;
}

int moduleSetGlobal(JStarVM* vm, ObjModule* mod, ObjString* key, Value val) {
    int offset;
    if(hashTableIntGet(&mod->globalNames, key, &offset)) {
        push(vm, val);
        moduleSetGlobalAtOffset(vm, mod, offset, val);
        pop(vm);
        return offset;
    } else {
        int offset = mod->globalsCount++;
        hashTableIntPut(&mod->globalNames, key, offset);
        push(vm, val);
        moduleSetGlobalAtOffset(vm, mod, offset, val);
        pop(vm);
        return offset;
    }
}

bool moduleGetGlobal(ObjModule* mod, ObjString* key, Value* out) {
    int offset = moduleGetGlobalOffset(mod, key);
    if(offset == -1) return false;
    *out = mod->globals[offset];
    return true;
}

int moduleGetGlobalOffset(ObjModule* mod, ObjString* key) {
    int offset;
    if(!hashTableIntGet(&mod->globalNames, key, &offset)) return -1;
    return offset >= mod->globalsCount ? -1 : offset;
}

void moduleSetPath(JStarVM* vm, ObjModule* mod, const char* path) {
    mod->path = copyCStringInterned(vm, path);
    push(vm, OBJ_VAL(mod->path));
    moduleSetGlobal(vm, mod, copyCStringInterned(vm, MOD_PATH), OBJ_VAL(mod->path));
    pop(vm);
}

void listAppend(JStarVM* vm, ObjList* lst, Value val) {
    push(vm, val);
    arrayAppendGC(vm, lst, val);
    pop(vm);
}

void listInsert(JStarVM* vm, ObjList* lst, size_t index, Value val) {
    JSR_ASSERT(index <= lst->count, "Index out of bounds");
    arrayAppendGC(vm, lst, NULL_VAL);
    memmove(lst->items + index + 1, lst->items + index, sizeof(Value) * (lst->count - index - 1));
    lst->items[index] = val;
}

void listRemove(ObjList* lst, size_t index) {
    JSR_ASSERT(index < lst->count, "Index out of bounds");
    memmove(lst->items + index, lst->items + index + 1, sizeof(Value) * (lst->count - index - 1));
    lst->count--;
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

void stacktraceDump(JStarVM* vm, ObjStackTrace* st, Frame* f) {
    FrameRecord record = {0};

    switch(f->fn->type) {
    case OBJ_CLOSURE: {
        ObjClosure* closure = (ObjClosure*)f->fn;
        ObjFunction* fn = closure->fn;
        Code* code = &fn->code;

        size_t op = f->ip - code->bytecode.items - 1;
        if(op >= code->bytecode.count) {
            op = code->bytecode.count - 1;
        }

        record.line = getBytecodeSrcLine(code, op);
        record.path = fn->base.module->path;
        record.moduleName = fn->base.module->name;
        record.funcName = fn->base.name;
        break;
    }
    case OBJ_NATIVE: {
        ObjNative* nat = (ObjNative*)f->fn;
        record.line = 0;
        record.path = nat->base.module->path;
        record.moduleName = nat->base.module->name;
        record.funcName = nat->base.name;
        break;
    }
    default:
        JSR_UNREACHABLE();
    }

    arrayAppendGC(vm, &st->records, record);
}

Value* getValues(Obj* obj, size_t* count) {
    JSR_ASSERT(obj->type == OBJ_LIST || obj->type == OBJ_TUPLE, "Object isn't a Tuple or List.");
    switch(obj->type) {
    case OBJ_LIST: {
        ObjList* lst = (ObjList*)obj;
        *count = lst->count;
        return lst->items;
    }
    case OBJ_TUPLE: {
        ObjTuple* tup = (ObjTuple*)obj;
        *count = tup->count;
        return tup->items;
    }
    default:
        JSR_UNREACHABLE();
    }
}

FunctionBase* getFunctionBase(Obj* fn) {
    switch(fn->type) {
    case OBJ_CLOSURE:
        return &((ObjClosure*)fn)->fn->base;
    case OBJ_NATIVE:
        return &((ObjNative*)fn)->base;
    case OBJ_BOUND_METHOD:
        return getFunctionBase(((ObjBoundMethod*)fn)->method);
    default:
        JSR_UNREACHABLE();
    }
}

ObjString* jsrBufferToString(JStarBuffer* b) {
    // Shrink to fit the buffer
    char* data = gcAlloc(b->vm, b->data, b->capacity, b->size + 1);
    ObjClass* strClass = b->vm->coreClasses[CORE_CLASS_STR];
    ObjString* s = (ObjString*)newObj(b->vm, sizeof(*s), strClass, OBJ_STRING);
    s->interned = false;
    s->length = b->size;
    s->data = data;
    s->hash = 0;
    s->data[s->length] = '\0';
    *b = (JStarBuffer){0};
    return s;
}

// -----------------------------------------------------------------------------
// DEBUG
// -----------------------------------------------------------------------------

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
        putc('"', stdout);
        printEscaped((ObjString*)o);
        putc('"', stdout);
        break;
    case OBJ_FUNCTION: {
        ObjFunction* f = (ObjFunction*)o;
        if(f->base.module->name->length != 0) {
            printf("<func %s.%s:%d>", f->base.module->name->data, f->base.name->data,
                   f->base.argsCount);
        } else {
            printf("<func %s:%d>", f->base.name->data, f->base.argsCount);
        }
        break;
    }
    case OBJ_NATIVE: {
        ObjNative* n = (ObjNative*)o;
        if(n->base.module->name->length != 0) {
            printf("<native %s.%s:%d>", n->base.module->name->data, n->base.name->data,
                   n->base.argsCount);
        } else {
            printf("<native %s:%d>", n->base.name->data, n->base.argsCount);
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
        for(size_t i = 0; i < lst->count; i++) {
            printValue(lst->items[i]);
            if(i != lst->count - 1) printf(", ");
        }
        printf("]");
        break;
    }
    case OBJ_TUPLE: {
        ObjTuple* t = (ObjTuple*)o;
        printf("(");
        for(size_t i = 0; i < t->count; i++) {
            printValue(t->items[i]);
            if(i != t->count - 1) printf(", ");
        }
        printf(")");
        break;
    }
    case OBJ_TABLE: {
        ObjTable* t = (ObjTable*)o;
        printf("{");
        if(t->entries != NULL) {
            for(size_t i = 0; i < t->sizeMask + 1; i++) {
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
        printf("<bound method ");
        printValue(b->receiver);
        printf(":%s>", getFunctionBase(b->method)->name->data);
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
