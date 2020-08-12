#include "object.h"

#include <stdio.h>
#include <string.h>

#include "gc.h"
#include "vm.h"

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

static void initCommon(FnCommon* c, ObjModule* module, uint8_t argc, Value* defaults,
                       uint8_t defaultc, bool varg) {
    c->name = NULL;
    c->module = module;
    c->argsCount = argc;
    c->defaults = defaults;
    c->defaultc = defaultc;
    c->vararg = varg;
}

static Value* allocateDefaultArray(JStarVM* vm, uint8_t defaultCount) {
    if(defaultCount == 0) return NULL;
    Value* defaultArray = GC_ALLOC(vm, sizeof(Value) * defaultCount);
    for(int i = 0; i < defaultCount; i++) {
        defaultArray[i] = NULL_VAL;
    }
    return defaultArray;
}

ObjFunction* newFunction(JStarVM* vm, ObjModule* module, uint8_t argc, uint8_t defCount,
                         bool varg) {
    Value* defaults = allocateDefaultArray(vm, defCount);
    ObjFunction* f = (ObjFunction*)newObj(vm, sizeof(*f), vm->funClass, OBJ_FUNCTION);
    initCommon(&f->c, module, argc, defaults, defCount, varg);
    f->upvaluec = 0;
    initCode(&f->code);
    return f;
}

ObjNative* newNative(JStarVM* vm, ObjModule* module, uint8_t argc, uint8_t defCount, bool varg) {
    Value* defaults = allocateDefaultArray(vm, defCount);
    ObjNative* n = (ObjNative*)newObj(vm, sizeof(*n), vm->funClass, OBJ_NATIVE);
    initCommon(&n->c, module, argc, defaults, defCount, varg);
    return n;
}

ObjClass* newClass(JStarVM* vm, ObjString* name, ObjClass* superCls) {
    ObjClass* cls = (ObjClass*)newObj(vm, sizeof(*cls), vm->clsClass, OBJ_CLASS);
    cls->name = name;
    cls->superCls = superCls;
    initHashTable(&cls->methods);
    return cls;
}

ObjInstance* newInstance(JStarVM* vm, ObjClass* cls) {
    ObjInstance* inst = (ObjInstance*)newObj(vm, sizeof(*inst), cls, OBJ_INST);
    initHashTable(&inst->fields);
    return inst;
}

ObjClosure* newClosure(JStarVM* vm, ObjFunction* fn) {
    ObjClosure* c = (ObjClosure*)newVarObj(vm, sizeof(*c), sizeof(ObjUpvalue*), fn->upvaluec,
                                           vm->funClass, OBJ_CLOSURE);
    memset(c->upvalues, 0, sizeof(ObjUpvalue*) * fn->upvaluec);
    c->upvalueCount = fn->upvaluec;
    c->fn = fn;
    return c;
}

ObjModule* newModule(JStarVM* vm, ObjString* name) {
    ObjModule* module = (ObjModule*)newObj(vm, sizeof(*module), vm->modClass, OBJ_MODULE);
    module->name = name;
    initHashTable(&module->globals);
    module->natives.dynlib = NULL;
    module->natives.registry = NULL;
    return module;
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
    bm->bound = bound;
    bm->method = method;
    return bm;
}

ObjTuple* newTuple(JStarVM* vm, size_t size) {
    if(size == 0 && vm->emptyTup) return vm->emptyTup;
    ObjTuple* tuple =
        (ObjTuple*)newVarObj(vm, sizeof(*tuple), sizeof(Value), size, vm->tupClass, OBJ_TUPLE);
    tuple->size = size;
    for(uint8_t i = 0; i < tuple->size; i++) {
        tuple->arr[i] = NULL_VAL;
    }
    return tuple;
}

ObjUserdata* newUserData(JStarVM* vm, size_t size, void (*finalize)(void*)) {
    ObjUserdata* udata = (ObjUserdata*)newVarObj(vm, sizeof(*udata), sizeof(uint8_t), size,
                                                 vm->udataClass, OBJ_USERDATA);
    udata->size = size;
    udata->finalize = finalize;
    return udata;
}

#define ST_DEF_SIZE 16

ObjStackTrace* newStackTrace(JStarVM* vm) {
    ObjStackTrace* st = (ObjStackTrace*)newObj(vm, sizeof(*st), vm->stClass, OBJ_STACK_TRACE);
    st->lastTracedFrame = -1;
    st->recordSize = 0;
    st->recordCount = 0;
    st->records = NULL;
    return st;
}

void stRecordFrame(JStarVM* vm, ObjStackTrace* st, Frame* f, int depth) {
    if(st->lastTracedFrame == depth) return;
    st->lastTracedFrame = depth;

    if(st->recordCount + 1 >= st->recordSize) {
        size_t oldSize = sizeof(FrameRecord) * st->recordSize;
        st->recordSize = st->records ? st->recordSize * 2 : 4;
        st->records = GCallocate(vm, st->records, oldSize, sizeof(FrameRecord) * st->recordSize);
    }

    FrameRecord* record = &st->records[st->recordCount++];
    record->funcName = NULL;
    record->moduleName = NULL;

    switch(f->fn->type) {
    case OBJ_CLOSURE: {
        ObjFunction* fn = ((ObjClosure*)f->fn)->fn;
        Code* code = &fn->code;
        size_t op = f->ip - code->bytecode - 1;
        record->line = getBytecodeSrcLine(code, op);
        record->moduleName = fn->c.module->name;
        record->funcName = fn->c.name;
        break;
    }
    case OBJ_NATIVE: {
        ObjNative* nat = (ObjNative*)f->fn;
        record->line = -1;
        record->moduleName = nat->c.module->name;
        record->funcName = nat->c.name;
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

#define LIST_DEF_SZ    8
#define LIST_GROW_RATE 2

ObjList* newList(JStarVM* vm, size_t startSize) {
    size_t size = startSize == 0 ? LIST_DEF_SZ : startSize;
    Value* arr = GC_ALLOC(vm, sizeof(Value) * size);
    ObjList* l = (ObjList*)newObj(vm, sizeof(*l), vm->lstClass, OBJ_LIST);
    l->size = size;
    l->count = 0;
    l->arr = arr;
    return l;
}

static void growList(JStarVM* vm, ObjList* lst) {
    size_t newSize = lst->size * LIST_GROW_RATE;
    lst->arr = GCallocate(vm, lst->arr, sizeof(Value) * lst->size, sizeof(Value) * newSize);
    lst->size = newSize;
}

void listAppend(JStarVM* vm, ObjList* lst, Value val) {
    // if the list get resized a GC may kick in, so push val as root
    if(lst->count + 1 > lst->size) {
        push(vm, val);
        growList(vm, lst);
        pop(vm);
    }
    lst->arr[lst->count++] = val;
}

void listInsert(JStarVM* vm, ObjList* lst, size_t index, Value val) {
    // if the list get resized a GC may kick in, so push val as root
    if(lst->count + 1 > lst->size) {
        push(vm, val);
        growList(vm, lst);
        pop(vm);
    }

    Value* arr = lst->arr;
    for(size_t i = lst->count; i > index; i--) {
        arr[i] = arr[i - 1];
    }
    arr[index] = val;
    lst->count++;
}

void listRemove(JStarVM* vm, ObjList* lst, size_t index) {
    Value* arr = lst->arr;
    for(size_t i = index + 1; i < lst->count; i++) {
        arr[i - 1] = arr[i];
    }
    lst->count--;
}

ObjTable* newTable(JStarVM* vm) {
    ObjTable* table = (ObjTable*)newObj(vm, sizeof(*table), vm->tableClass, OBJ_TABLE);
    table->sizeMask = 0;
    table->numEntries = 0;
    table->count = 0;
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
    uint32_t hash = hashString(str, length);
    ObjString* internedString = hashTableGetString(&vm->strings, str, length, hash);
    if(internedString == NULL) {
        internedString = allocateString(vm, length);
        memcpy(internedString->data, str, length);
        internedString->hash = hash;
        internedString->interned = true;
        hashTablePut(&vm->strings, internedString, NULL_VAL);
    }
    return internedString;
}

/**
 * =========================================================
 *  API - JStarBuffer function implementation
 * =========================================================
 */

ObjString* jsrBufferToString(JStarBuffer* b) {
    char* data = GCallocate(b->vm, b->data, b->size, b->len + 1);
    data[b->len] = '\0';
    ObjString* s = (ObjString*)newObj(b->vm, sizeof(*s), b->vm->strClass, OBJ_STRING);
    s->interned = false;
    s->length = b->len;
    s->data = data;
    s->hash = 0;
    b->data = NULL;
    b->vm = NULL;
    b->len = b->size = 0;
    return s;
}

#define BUF_DEF_SZ 16

static void jsrBufGrow(JStarBuffer* b, size_t len) {
    size_t newSize = b->size;
    while(newSize < b->len + len) {
        newSize <<= 1;
    }
    char* newData = GCallocate(b->vm, b->data, b->size, newSize);
    b->size = newSize;
    b->data = newData;
}

void jsrBufferInit(JStarVM* vm, JStarBuffer* b) {
    jsrBufferInitSz(vm, b, BUF_DEF_SZ);
}

void jsrBufferInitSz(JStarVM* vm, JStarBuffer* b, size_t size) {
    if(size < BUF_DEF_SZ) size = BUF_DEF_SZ;
    b->vm = vm;
    b->size = size;
    b->len = 0;
    b->data = GC_ALLOC(vm, size);
}

void jsrBufferAppend(JStarBuffer* b, const char* str, size_t len) {
    if(b->len + len >= b->size) {
        jsrBufGrow(b, len + 1);  // the >= and the +1 are for the terminating NUL
    }
    memcpy(&b->data[b->len], str, len);
    b->len += len;
    b->data[b->len] = '\0';
}

void jsrBufferAppendstr(JStarBuffer* b, const char* str) {
    jsrBufferAppend(b, str, strlen(str));
}

void jsrBufferAppendvf(JStarBuffer* b, const char* fmt, va_list ap) {
    size_t availableSpace = b->size - b->len;

    va_list cpy;
    va_copy(cpy, ap);
    size_t written = vsnprintf(&b->data[b->len], availableSpace, fmt, cpy);
    va_end(cpy);

    // Not enough space, need to grow and retry
    if(written >= availableSpace) {
        jsrBufGrow(b, written + 1);
        availableSpace = b->size - b->len;
        va_copy(cpy, ap);
        written = vsnprintf(&b->data[b->len], availableSpace, fmt, cpy);
        ASSERT(written < availableSpace, "Buffer still to small");
        va_end(cpy);
    }

    b->len += written;
}

void jsrBufferAppendf(JStarBuffer* b, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    jsrBufferAppendvf(b, fmt, ap);
    va_end(ap);
}

void jsrBufferTrunc(JStarBuffer* b, size_t len) {
    if(len >= b->len) return;
    b->len = len;
    b->data[len] = '\0';
}

void jsrBufferCut(JStarBuffer* b, size_t len) {
    if(len == 0 || len > b->len) return;
    memmove(b->data, b->data + len, b->len - len);
    b->len -= len;
    b->data[b->len] = '\0';
}

void jsrBufferReplaceChar(JStarBuffer* b, size_t start, char c, char r) {
    for(size_t i = start; i < b->len; i++) {
        if(b->data[i] == c) {
            b->data[i] = r;
        }
    }
}

void jsrBufferPrepend(JStarBuffer* b, const char* str, size_t len) {
    if(b->len + len >= b->size) {
        jsrBufGrow(b, len + 1);  // the >= and the +1 are for the terminating NUL
    }
    memmove(b->data + len, b->data, b->len);
    memcpy(b->data, str, len);
    b->len += len;
    b->data[b->len] = '\0';
}

void jsrBufferPrependstr(JStarBuffer* b, const char* str) {
    jsrBufferPrepend(b, str, strlen(str));
}

void jsrBufferAppendChar(JStarBuffer* b, char c) {
    if(b->len + 1 >= b->size) jsrBufGrow(b, 2);
    b->data[b->len++] = c;
    b->data[b->len] = '\0';
}

void jsrBufferClear(JStarBuffer* b) {
    b->len = 0;
    b->data[0] = '\0';
}

void jsrBufferPush(JStarBuffer* b) {
    JStarVM* vm = b->vm;
    push(vm, OBJ_VAL(jsrBufferToString(b)));
}

void jsrBufferFree(JStarBuffer* b) {
    if(b->data == NULL) return;
    GC_FREE_ARRAY(b->vm, char, b->data, b->size);
    b->data = NULL;
    b->vm = NULL;
    b->len = b->size = 0;
}

// Debug logging functions

#ifdef JSTAR_DBG_PRINT_GC
const char* ObjTypeNames[] = {
    #define ENUM_STRING(elem) #elem,
    OBJTYPE(ENUM_STRING)
    #undef ENUM_STRING
};
#endif

void printObj(Obj* o) {
    switch(o->type) {
    case OBJ_STRING:
        printf("%s", ((ObjString*)o)->data);
        break;
    case OBJ_FUNCTION: {
        ObjFunction* f = (ObjFunction*)o;
        if(f->c.name != NULL) {
            printf("<func %s:%d>", f->c.name->data, f->c.argsCount);
        } else {
            printf("<func %d>", f->c.argsCount);
        }
        break;
    }
    case OBJ_NATIVE: {
        ObjNative* n = (ObjNative*)o;
        if(n->c.name != NULL) {
            printf("<native %s:%d>", n->c.name->data, n->c.argsCount);
        } else {
            printf("<native %d>", n->c.argsCount);
        }
        break;
    }
    case OBJ_CLASS: {
        ObjClass* cls = (ObjClass*)o;
        printf("<class %s:%s>", cls->name->data,
               cls->superCls == NULL ? "" : cls->superCls->name->data);
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
        ObjList* l = (ObjList*)o;
        printf("[");
        for(size_t i = 0; i < l->count; i++) {
            printValue(l->arr[i]);
            if(i != l->count - 1) printf(", ");
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

        char* name;
        if(b->method->type == OBJ_CLOSURE) {
            name = ((ObjClosure*)b->method)->fn->c.name->data;
        } else {
            name = ((ObjNative*)b->method)->c.name->data;
        }

        printf("<bound method ");
        printValue(b->bound);
        printf(":%s>", name);
        break;
    }
    case OBJ_STACK_TRACE:
        printf("<stacktrace %p>", (void*)o);
        break;
    case OBJ_CLOSURE:
        printf("<closure %p>", (void*)o);
        break;
    case OBJ_UPVALUE:
        printf("<upvalue %p>", (void*)o);
        break;
    case OBJ_USERDATA:
        printf("<userdata %p", (void*)o);
        break;
    }
}
