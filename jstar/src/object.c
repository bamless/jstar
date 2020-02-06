#include "object.h"
#include "memory.h"
#include "vm.h"

#include <string.h>
#include <stdio.h>

static Obj *newObj(JStarVM *vm, size_t size, ObjClass *cls, ObjType type) {
    Obj *o = GC_ALLOC(vm, size);
    o->cls = cls;
    o->type = type;
    o->reached = false;
    o->next = vm->objects;
    vm->objects = o;
    return o;
}

static Obj *newVarObj(JStarVM *vm, size_t size, size_t varSize, 
    size_t count, ObjClass *cls, ObjType type)
{
    return newObj(vm, size + varSize * count, cls, type);
}

static void initCallable(Callable *c, ObjModule *module, ObjString *name,
    uint8_t argc, Value *defArr, uint8_t defc)
{
    c->argsCount = argc;
    c->defaultc = defc;
    c->vararg = false;
    c->defaults = defArr;
    c->module = module;
    c->name = name;
}

ObjFunction *newFunction(JStarVM *vm, ObjModule *module, ObjString *name, 
    uint8_t argc, uint8_t defc)
{
    Value *defArr = defc > 0 ? GC_ALLOC(vm, sizeof(Value) * defc) : NULL;
    memset(defArr, 0, defc * sizeof(Value));
    ObjFunction *f = (ObjFunction *)newObj(vm, sizeof(*f), vm->funClass, OBJ_FUNCTION);
    initCallable(&f->c, module, name, argc, defArr, defc);
    f->upvaluec = 0;
    initChunk(&f->chunk);
    return f;
}

ObjNative *newNative(JStarVM *vm, ObjModule *module, ObjString *name, 
    uint8_t argc, JStarNative fn, uint8_t defc)
{
    Value *defArr = defc > 0 ? GC_ALLOC(vm, sizeof(Value) * defc) : NULL;
    memset(defArr, 0, defc * sizeof(Value));
    ObjNative *n = (ObjNative *)newObj(vm, sizeof(*n), vm->funClass, OBJ_NATIVE);
    initCallable(&n->c, module, name, argc, defArr, defc);
    n->fn = fn;
    return n;
}

ObjClass *newClass(JStarVM *vm, ObjString *name, ObjClass *superCls) {
    ObjClass *cls = (ObjClass *)newObj(vm, sizeof(*cls), vm->clsClass, OBJ_CLASS);
    cls->name = name;
    cls->superCls = superCls;
    initHashTable(&cls->methods);
    return cls;
}

ObjInstance *newInstance(JStarVM *vm, ObjClass *cls) {
    ObjInstance *inst = (ObjInstance *)newObj(vm, sizeof(*inst), cls, OBJ_INST);
    initHashTable(&inst->fields);
    return inst;
}

ObjClosure *newClosure(JStarVM *vm, ObjFunction *fn) {
    ObjClosure *c = (ObjClosure *)newVarObj(vm, sizeof(*c), sizeof(ObjUpvalue *), fn->upvaluec,
                                            vm->funClass, OBJ_CLOSURE);
    memset(c->upvalues, 0, sizeof(ObjUpvalue *) * fn->upvaluec);
    c->upvalueCount = fn->upvaluec;
    c->fn = fn;
    return c;
}

ObjModule *newModule(JStarVM *vm, ObjString *name) {
    ObjModule *module = (ObjModule *)newObj(vm, sizeof(*module), vm->modClass, OBJ_MODULE);
    module->name = name;
    initHashTable(&module->globals);
    module->natives.dynlib = NULL;
    module->natives.registry = NULL;
    return module;
}

ObjUpvalue *newUpvalue(JStarVM *vm, Value *addr) {
    ObjUpvalue *upvalue = (ObjUpvalue *)newObj(vm, sizeof(*upvalue), NULL, OBJ_UPVALUE);
    upvalue->addr = addr;
    upvalue->closed = NULL_VAL;
    upvalue->next = NULL;
    return upvalue;
}

ObjBoundMethod *newBoundMethod(JStarVM *vm, Value b, Obj *method) {
    ObjBoundMethod *bm = (ObjBoundMethod *)newObj(vm, sizeof(*bm), vm->funClass, OBJ_BOUND_METHOD);
    bm->bound = b;
    bm->method = method;
    return bm;
}

ObjTuple *newTuple(JStarVM *vm, size_t size) {
    if(size == 0 && vm->emptyTup) return vm->emptyTup;
    ObjTuple *t = (ObjTuple *)newVarObj(vm, sizeof(*t), sizeof(Value), size, vm->tupClass, OBJ_TUPLE);
    t->size = size;
    for(uint8_t i = 0; i < t->size; i++) {
        t->arr[i] = NULL_VAL;
    }
    return t;
}

#define ST_DEF_SIZE 16

ObjStackTrace *newStackTrace(JStarVM *vm) {
    ObjStackTrace *st = (ObjStackTrace *)newObj(vm, sizeof(*st), vm->stClass, OBJ_STACK_TRACE);
    st->lastTracedFrame = -1;
    st->recordSize = 0;
    st->recordCount = 0;
    st->records = NULL;
    return st;
}

void stRecordFrame(JStarVM *vm, ObjStackTrace *st, Frame *f, int depth) {
    if(st->lastTracedFrame == depth) return;
    st->lastTracedFrame = depth;

    if(st->recordCount + 1 >= st->recordSize) {
        size_t oldSize = sizeof(FrameRecord) * st->recordSize;
        st->recordSize = st->records ? st->recordSize * 2 : 4;
        st->records = GCallocate(vm, st->records, oldSize, sizeof(FrameRecord) * st->recordSize);
    }
    
    FrameRecord *record = &st->records[st->recordCount++];
    record->funcName = NULL;
    record->moduleName = NULL;

    Callable *c = NULL;
    
    switch(f->fn.type) {
    case OBJ_CLOSURE:
        c =  &f->fn.as.closure->fn->c;
        Chunk *chunk = &f->fn.as.closure->fn->chunk;
        size_t op = f->ip - chunk->code - 1;
        record->line = getBytecodeSrcLine(chunk, op);
        break;
    case OBJ_NATIVE:
        c = &f->fn.as.native->c;
        record->line = -1;
        break;
    default:
        UNREACHABLE();
        break;
    }
    
    record->moduleName = c->module->name;
    record->funcName = c->name ? c->name : copyString(vm, "<main>", 6, true);
}

#define LIST_DEF_SZ 8
#define LIST_GROW_RATE 2

ObjList *newList(JStarVM *vm, size_t startSize) {
    size_t size = startSize == 0 ? LIST_DEF_SZ : startSize;
    Value *arr = GC_ALLOC(vm, sizeof(Value) * size);
    ObjList *l = (ObjList *)newObj(vm, sizeof(*l), vm->lstClass, OBJ_LIST);
    l->size = size;
    l->count = 0;
    l->arr = arr;
    return l;
}

static void growList(JStarVM *vm, ObjList *lst) {
    size_t newSize = lst->size * LIST_GROW_RATE;
    lst->arr = GCallocate(vm, lst->arr, sizeof(Value) * lst->size, sizeof(Value) * newSize);
    lst->size = newSize;
}

void listAppend(JStarVM *vm, ObjList *lst, Value val) {
    // if the list get resized a GC may kick in, so push val as root
    push(vm, val);
    if(lst->count + 1 > lst->size) {
        growList(vm, lst);
    }
    lst->arr[lst->count++] = val;
    pop(vm); // pop val
}

void listInsert(JStarVM *vm, ObjList *lst, size_t index, Value val) {
    // if the list get resized a GC may kick in, so push val as root
    push(vm, val);
    if(lst->count + 1 > lst->size) {
        growList(vm, lst);
    }

    Value *arr = lst->arr;
    for(size_t i = lst->count; i > index; i--) {
        arr[i] = arr[i - 1];
    }
    arr[index] = val;
    lst->count++;
    pop(vm);
}

void listRemove(JStarVM *vm, ObjList *lst, size_t index) {
    Value *arr = lst->arr;
    for(size_t i = index + 1; i < lst->count; i++) {
        arr[i - 1] = arr[i];
    }
    lst->count--;
}

ObjTable *newTable(JStarVM *vm) {
    ObjTable *table = (ObjTable *)newObj(vm, sizeof(*table), vm->tableClass, OBJ_TABLE);
    table->sizeMask = 0;
    table->numEntries = 0;
    table->count = 0;
    table->entries = NULL;
    return table;
}

ObjString *allocateString(JStarVM *vm, size_t length) {
    char *data = GC_ALLOC(vm, length + 1);
    ObjString *str = (ObjString *)newObj(vm, sizeof(*str), vm->strClass, OBJ_STRING);
    str->length = length;
    str->hash = 0;
    str->interned = false;
    str->data = data;
    str->data[str->length] = '\0';
    return str;
}

static ObjString *newString(JStarVM *vm, const char *cstring, size_t length) {
    ObjString *str = allocateString(vm, length);
    memcpy(str->data, cstring, length);
    return str;
}

ObjString *copyString(JStarVM *vm, const char *str, size_t length, bool intern) {
    if(intern) {
        uint32_t hash = hashString(str, length);
        ObjString *interned = hashTableGetString(&vm->strings, str, length, hash);
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

/**
 * =========================================================
 *  API - JStarBuffer function implementation
 * =========================================================
 */

ObjString *jsrBufferToString(JStarBuffer *b) {
    char *data = GCallocate(b->vm, b->data, b->size, b->len + 1);
    data[b->len] = '\0';
    ObjString *s = (ObjString *)newObj(b->vm, sizeof(*s), b->vm->strClass, OBJ_STRING);
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

static void jsrBufGrow(JStarBuffer *b, size_t len) {
    size_t newSize = b->size;
    while(newSize < b->len + len) {
        newSize <<= 1;
    }
    char *newData = GCallocate(b->vm, b->data, b->size, newSize);
    b->size = newSize;
    b->data = newData;
}

void jsrBufferInit(JStarVM *vm, JStarBuffer *b) {
    jsrBufferInitSz(vm, b, BUF_DEF_SZ);
}

void jsrBufferInitSz(JStarVM *vm, JStarBuffer *b, size_t size) {
    if(size < BUF_DEF_SZ) size = BUF_DEF_SZ;
    b->vm = vm;
    b->size = size;
    b->len = 0;
    b->data = GC_ALLOC(vm, size);
}

void jsrBufferAppend(JStarBuffer *b, const char *str, size_t len) {
    if(b->len + len >= b->size) {
        jsrBufGrow(b, len + 1); // the >= and the +1 are for the terminating NUL
    }
    memcpy(&b->data[b->len], str, len);
    b->len += len;
    b->data[b->len] = '\0';
}

void jsrBufferAppendstr(JStarBuffer *b, const char *str) {
    jsrBufferAppend(b, str, strlen(str));
}

void jsrBufferTrunc(JStarBuffer *b, size_t len) {
    if(len >= b->len) return;
    b->len = len;
    b->data[len] = '\0';
}

void jsrBufferCut(JStarBuffer *b, size_t len) {
    if(len == 0 || len > b->len) return;
    memmove(b->data, b->data + len, b->len - len);
    b->len -= len;
    b->data[b->len] = '\0';
}

void jsrBufferReplaceChar(JStarBuffer *b, size_t start, char c, char r) {
    for(size_t i = start; i < b->len; i++) {
        if(b->data[i] == c) {
            b->data[i] = r;
        }
    }
}

void jsrBufferPrepend(JStarBuffer *b, const char *str, size_t len) {
    if(b->len + len >= b->size) {
        jsrBufGrow(b, len + 1); // the >= and the +1 are for the terminating NUL
    }
    memmove(b->data + len, b->data, b->len);
    memcpy(b->data, str, len);
    b->len += len;
    b->data[b->len] = '\0';
}

void jsrBufferPrependstr(JStarBuffer *b, const char *str) {
    jsrBufferPrepend(b, str, strlen(str));
}

void jsrBufferAppendChar(JStarBuffer *b, char c) {
    if(b->len + 1 >= b->size) jsrBufGrow(b, 2);
    b->data[b->len++] = c;
    b->data[b->len] = '\0';
}

void jsrBufferClear(JStarBuffer *b) {
    b->len = 0;
    b->data[0] = '\0';
}

void jsrBufferPush(JStarBuffer *b) {
    JStarVM *vm = b->vm;
    push(vm, OBJ_VAL(jsrBufferToString(b)));
}

void jsrBufferFree(JStarBuffer *b) {
    if(b->data == NULL) return;
    GC_FREEARRAY(b->vm, char, b->data, b->size);
    b->data = NULL;
    b->vm = NULL;
    b->len = b->size = 0;
}

// Debug logging functions

#ifdef DBG_PRINT_GC
    DEFINE_TO_STRING(ObjType, OBJTYPE);
#endif

void printObj(Obj *o) {
    switch(o->type) {
    case OBJ_STRING:
        printf("%s", ((ObjString *)o)->data);
        break;
    case OBJ_FUNCTION: {
        ObjFunction *f = (ObjFunction *)o;
        if(f->c.name != NULL) {
            printf("<func %s:%d>", f->c.name->data, f->c.argsCount);
        } else {
            printf("<func %d>", f->c.argsCount);
        }
        break;
    }
    case OBJ_NATIVE: {
        ObjNative *n = (ObjNative *)o;
        if(n->c.name != NULL) {
            printf("<native %s:%d>", n->c.name->data, n->c.argsCount);
        } else {
            printf("<native %d>", n->c.argsCount);
        }
        break;
    }
    case OBJ_CLASS: {
        ObjClass *cls = (ObjClass *)o;
        printf("<class %s:%s>", cls->name->data,
               cls->superCls == NULL ? "" : cls->superCls->name->data);
        break;
    }
    case OBJ_INST: {
        ObjInstance *i = (ObjInstance *)o;
        printf("<instance %s>", i->base.cls->name->data);
        break;
    }
    case OBJ_MODULE: {
        ObjModule *m = (ObjModule *)o;
        printf("<module %s>", m->name->data);
        break;
    }
    case OBJ_LIST: {
        ObjList *l = (ObjList *)o;
        printf("[");
        for(size_t i = 0; i < l->count; i++) {
            printValue(l->arr[i]);
            if(i != l->count - 1) printf(", ");
        }
        printf("]");
        break;
    }
    case OBJ_TUPLE: {
        ObjTuple *t = (ObjTuple *)o;
        printf("(");
        for(size_t i = 0; i < t->size; i++) {
            printValue(t->arr[i]);
            if(i != t->size - 1) printf(", ");
        }
        printf(")");
        break;
    }
    case OBJ_TABLE: {
        ObjTable *t = (ObjTable *)o;
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
        ObjBoundMethod *b = (ObjBoundMethod *)o;

        char *name;
        if(b->method->type == OBJ_FUNCTION)
            name = ((ObjFunction *)b->method)->c.name->data;
        else
            name = ((ObjNative *)b->method)->c.name->data;
  
        printf("<bound method ");
        printValue(b->bound);
        printf(":%s>", name);
        break;
    }
    case OBJ_STACK_TRACE:
        printf("<stacktrace %p>", (void *)o);
        break;
    case OBJ_CLOSURE:
        printf("<closure %p>", (void *)o);
        break;
    case OBJ_UPVALUE:
        printf("<upvalue %p>", (void *)o);
        break;
    }
}
