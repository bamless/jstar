#include "core.h"
#include "import.h"
#include "memory.h"
#include "object.h"
#include "vm.h"

#include "builtin/modules.h"

#include <errno.h>
#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static ObjClass *createClass(JStarVM *vm, ObjModule *m, ObjClass *sup, const char *name) {
    ObjString *n = copyString(vm, name, strlen(name), true);
    push(vm, OBJ_VAL(n));
    ObjClass *c = newClass(vm, n, sup);
    pop(vm);

    hashTablePut(&m->globals, n, OBJ_VAL(c));
    return c;
}

static Value getDefinedName(JStarVM *vm, ObjModule *m, const char *name) {
    Value v = NULL_VAL;
    hashTableGet(&m->globals, copyString(vm, name, strlen(name), true), &v);
    return v;
}

static void defMethod(JStarVM *vm, ObjModule *m, ObjClass *cls, JStarNative n, const char *name,
                      uint8_t argc) {
    ObjString *strName = copyString(vm, name, strlen(name), true);
    push(vm, OBJ_VAL(strName));
    ObjNative *native = newNative(vm, m, strName, argc, n, 0);
    pop(vm);

    hashTablePut(&cls->methods, strName, OBJ_VAL(native));
}

static uint64_t hash64(uint64_t x) {
    x = (x ^ (x >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
    x = (x ^ (x >> 27)) * UINT64_C(0x94d049bb133111eb);
    x = x ^ (x >> 31);
    return x;
}

// class Object
static JSR_NATIVE(jsr_Object_string) {
    Obj *o = AS_OBJ(vm->apiStack[0]);
    char str[256];
    snprintf(str, 255, "<%s@%p>", o->cls->name->data, (void *)o);
    jsrPushString(vm, str);
    return true;
}

static JSR_NATIVE(jsr_Object_hash) {
    uint64_t x = hash64((uint64_t)AS_OBJ(vm->apiStack[0]));
    jsrPushNumber(vm, (uint32_t)x);
    return true;
}

static JSR_NATIVE(jsr_Object_eq) {
    jsrPushBoolean(vm, valueEquals(vm->apiStack[0], vm->apiStack[1]));
    return true;
}
// end

// class Class
static JSR_NATIVE(jsr_Class_getName) {
    push(vm, OBJ_VAL(AS_CLASS(vm->apiStack[0])->name));
    return true;
}

static JSR_NATIVE(jsr_Class_string) {
    Obj *o = AS_OBJ(vm->apiStack[0]);
    char str[256];
    snprintf(str, 255, "<Class %s@%p>", ((ObjClass *)o)->name->data, (void *)o);
    jsrPushString(vm, str);
    return true;
}
// end

void initCoreLibrary(JStarVM *vm) {
    ObjString *name = copyString(vm, JSR_CORE_MODULE, strlen(JSR_CORE_MODULE), true);

    push(vm, OBJ_VAL(name));
    ObjModule *core = newModule(vm, name);
    setModule(vm, core->name, core);
    vm->core = core;
    pop(vm);

    // Setup the class object. It will be the class of every other class
    vm->clsClass = createClass(vm, core, NULL, "Class");
    vm->clsClass->base.cls = vm->clsClass; // Class is the class of itself

    // Setup the base class of the object hierarchy
    vm->objClass = createClass(vm, core, NULL, "Object"); // Object has no superclass
    defMethod(vm, core, vm->objClass, &jsr_Object_string, "__string__", 0);
    defMethod(vm, core, vm->objClass, &jsr_Object_hash, "__hash__", 0);
    defMethod(vm, core, vm->objClass, &jsr_Object_eq, "__eq__", 1);

    // Patch up Class object information
    vm->clsClass->superCls = vm->objClass;
    hashTableMerge(&vm->clsClass->methods, &vm->objClass->methods);
    defMethod(vm, core, vm->clsClass, &jsr_Class_getName, "getName", 0);
    defMethod(vm, core, vm->clsClass, &jsr_Class_string, "__string__", 0);

    jsrEvaluateModule(vm, JSR_CORE_MODULE, JSR_CORE_MODULE, readBuiltInModule(JSR_CORE_MODULE));

    vm->strClass   = AS_CLASS(getDefinedName(vm, core, "String"));
    vm->boolClass  = AS_CLASS(getDefinedName(vm, core, "Boolean"));
    vm->lstClass   = AS_CLASS(getDefinedName(vm, core, "List"));
    vm->numClass   = AS_CLASS(getDefinedName(vm, core, "Number"));
    vm->funClass   = AS_CLASS(getDefinedName(vm, core, "Function"));
    vm->modClass   = AS_CLASS(getDefinedName(vm, core, "Module"));
    vm->nullClass  = AS_CLASS(getDefinedName(vm, core, "Null"));
    vm->stClass    = AS_CLASS(getDefinedName(vm, core, "StackTrace"));
    vm->tupClass   = AS_CLASS(getDefinedName(vm, core, "Tuple"));
    vm->excClass   = AS_CLASS(getDefinedName(vm, core, "Exception"));
    vm->tableClass = AS_CLASS(getDefinedName(vm, core, "Table"));

    core->base.cls = vm->modClass;

    // Patch up the class field of any string or function that was allocated
    // before the creation of their corresponding class object
    for(Obj *o = vm->objects; o != NULL; o = o->next) {
        if(o->type == OBJ_STRING) {
            o->cls = vm->strClass;
        } else if(o->type == OBJ_CLOSURE || o->type == OBJ_FUNCTION || o->type == OBJ_NATIVE) {
            o->cls = vm->funClass;
        }
    }
}

JSR_NATIVE(jsr_int) {
    if(jsrIsNumber(vm, 1)) {
        jsrPushNumber(vm, trunc(jsrGetNumber(vm, 1)));
        return true;
    }
    if(jsrIsString(vm, 1)) {
        char *end = NULL;
        const char *nstr = jsrGetString(vm, 1);
        long long n = strtoll(nstr, &end, 10);

        if((n == 0 && end == nstr) || *end != '\0') {
            JSR_RAISE(vm, "InvalidArgException", "\"%s\".", nstr);
        }
        if(n == LLONG_MAX) {
            JSR_RAISE(vm, "InvalidArgException", "Overflow: \"%s\".", nstr);
        }
        if(n == LLONG_MIN) {
            JSR_RAISE(vm, "InvalidArgException", "Underflow: \"%s\".", nstr);
        }

        jsrPushNumber(vm, n);
        return true;
    }

    JSR_RAISE(vm, "TypeException", "Argument must be a number or a string.");
}

JSR_NATIVE(jsr_num) {
    if(jsrIsNumber(vm, 1)) {
        jsrPushNumber(vm, jsrGetNumber(vm, 1));
        return true;
    }
    if(jsrIsString(vm, 1)) {
        errno = 0;

        char *end = NULL;
        const char *nstr = jsrGetString(vm, 1);
        double n = strtod(nstr, &end);

        if((n == 0 && end == nstr) || *end != '\0') {
            JSR_RAISE(vm, "InvalidArgException", "\"%s\".", nstr);
        }
        if(n == HUGE_VAL || n == -HUGE_VAL) {
            JSR_RAISE(vm, "InvalidArgException", "Overflow: \"%s\".", nstr);
        }
        if(n == 0 && errno == ERANGE) {
            JSR_RAISE(vm, "InvalidArgException", "Underflow: \"%s\".", nstr);
        }

        jsrPushNumber(vm, n);
        return true;
    }

    JSR_RAISE(vm, "TypeException", "Argument must be a number or a string.");
}

JSR_NATIVE(jsr_isInt) {
    if(jsrIsNumber(vm, 1)) {
        double n = jsrGetNumber(vm, 1);
        jsrPushBoolean(vm, trunc(n) == n);
        return true;
    }
    jsrPushBoolean(vm, false);
    return true;
}

JSR_NATIVE(jsr_char) {
    if(!jsrCheckInt(vm, 1, "num")) return false;
    char c = jsrGetNumber(vm, 1);
    jsrPushStringSz(vm, &c, 1);
    return true;
}

JSR_NATIVE(jsr_ascii) {
    if(!jsrCheckStr(vm, 1, "arg")) return false;

    const char *str = jsrGetString(vm, 1);
    if(strlen(str) != 1) JSR_RAISE(vm, "InvalidArgException", "arg must be a String of length 1");

    char c = str[0];
    jsrPushNumber(vm, (double)c);
    return true;
}

JSR_NATIVE(jsr_print) {
    jsrPushValue(vm, 1);
    if(jsrCallMethod(vm, "__string__", 0) != VM_EVAL_SUCCESS) return false;
    if(!jsrIsString(vm, -1)) JSR_RAISE(vm, "TypeException", "s.__string__() didn't return a String");

    printf("%s", jsrGetString(vm, -1));
    jsrPop(vm);

    JSR_FOREACH(2, {
        if(jsrCallMethod(vm, "__string__", 0) != VM_EVAL_SUCCESS) return false;
        if(!jsrIsString(vm, -1)) {
            JSR_RAISE(vm, "TypeException", "__string__() didn't return a String");
        }
        printf(" %s", jsrGetString(vm, -1));
        jsrPop(vm);
    }, );

    printf("\n");

    jsrPushNull(vm);
    return true;
}

JSR_NATIVE(jsr_eval) {
    if(!jsrCheckStr(vm, 1, "source")) return false;
    if(vm->frameCount < 1) {
        JSR_RAISE(vm, "Exception", "eval() can only be called by another function");
    }

    ObjModule *mod;
    Frame *prevFrame = &vm->frames[vm->frameCount - 2];
    if(prevFrame->fn.type == OBJ_CLOSURE)
        mod = prevFrame->fn.as.closure->fn->c.module;
    else
        mod = prevFrame->fn.as.native->c.module;

    EvalResult res = jsrEvaluateModule(vm, "<string>", mod->name->data, jsrGetString(vm, 1));
    jsrPushBoolean(vm, res == VM_EVAL_SUCCESS);
    return true;
}

JSR_NATIVE(jsr_type) {
    push(vm, OBJ_VAL(getClass(vm, peek(vm))));
    return true;
}

// class Number
JSR_NATIVE(jsr_Number_string) {
    char str[24]; // enough for .*g with DBL_DIG
    snprintf(str, sizeof(str) - 1, "%.*g", DBL_DIG, jsrGetNumber(vm, 0));
    jsrPushString(vm, str);
    return true;
}

JSR_NATIVE(jsr_Number_hash) {
    double num = jsrGetNumber(vm, 0);
    if(num == 0) num = 0;
    union {
        double d;
        uint64_t r;
    } c = {.d = num};
    uint64_t n = hash64(c.r);
    jsrPushNumber(vm, (uint32_t)n);
    return true;
}
// end

// class Boolean
JSR_NATIVE(jsr_Boolean_string) {
    if(jsrGetBoolean(vm, 0))
        jsrPushString(vm, "true");
    else
        jsrPushString(vm, "false");
    return true;
}
// end

// class Null
JSR_NATIVE(jsr_Null_string) {
    jsrPushString(vm, "null");
    return true;
}
//

// class Function
JSR_NATIVE(jsr_Function_string) {
    const char *funType = NULL;
    const char *funName = NULL;
    const char *modName = NULL;

    switch(OBJ_TYPE(vm->apiStack[0])) {
    case OBJ_CLOSURE:
        funType = "function";
        funName = AS_CLOSURE(vm->apiStack[0])->fn->c.name->data;
        modName = AS_CLOSURE(vm->apiStack[0])->fn->c.module->name->data;
        break;
    case OBJ_NATIVE:
        funType = "native";
        funName = AS_NATIVE(vm->apiStack[0])->c.name->data;
        modName = AS_NATIVE(vm->apiStack[0])->c.module->name->data;
        break;
    case OBJ_BOUND_METHOD: {
        funType = "bound method";
        ObjBoundMethod *m = AS_BOUND_METHOD(vm->apiStack[0]);
        
        Callable *c;
        if(m->method->type == OBJ_CLOSURE)
            c = &((ObjClosure *)m->method)->fn->c;
        else
            c = &((ObjNative *)m->method)->c;
        
        funName = c->name->data;
        modName = c->module->name->data;
        break;
    }
    default:
        break;
    }


    char str[512] = {0};
    
    if(strcmp(modName, JSR_CORE_MODULE) == 0) {
        snprintf(str, sizeof(str) - 1, "<%s %s@%p>", funType, funName, AS_OBJ(vm->apiStack[0]));
    } else {
        snprintf(str, sizeof(str) - 1, "<%s %s.%s@%p>", funType, modName, funName, 
                 AS_OBJ(vm->apiStack[0]));
    }

    jsrPushString(vm, str);
    return true;
}
// end

// class Module
JSR_NATIVE(jsr_Module_string) {
    char str[256];
    ObjModule *m = AS_MODULE(vm->apiStack[0]);
    snprintf(str, sizeof(str) - 1, "<module %s@%p>", m->name->data, m);
    jsrPushString(vm, str);
    return true;
}
// end

// class List
JSR_NATIVE(jsr_List_new) {
    if(!jsrCheckInt(vm, 1, "size")) return false;
    double count = jsrGetNumber(vm, 1);

    if(count < 0) JSR_RAISE(vm, "TypeException", "size must be >= 0");
    ObjList *lst = newList(vm, count < 16 ? 16 : count);
    lst->count = count;
    push(vm, OBJ_VAL(lst));

    if(IS_CLOSURE(vm->apiStack[2]) || IS_NATIVE(vm->apiStack[2])) {
        for(size_t i = 0; i < lst->count; i++) {
            jsrPushValue(vm, 2);
            jsrPushNumber(vm, i);
            if(jsrCall(vm, 1) != VM_EVAL_SUCCESS) return false;
            lst->arr[i] = pop(vm);
        }
    } else {
        for(size_t i = 0; i < lst->count; i++) {
            lst->arr[i] = vm->apiStack[2];
        }
    }

    return true;
}

JSR_NATIVE(jsr_List_add) {
    ObjList *l = AS_LIST(vm->apiStack[0]);
    listAppend(vm, l, vm->apiStack[1]);
    jsrPushNull(vm);
    return true;
}

JSR_NATIVE(jsr_List_insert) {
    ObjList *l = AS_LIST(vm->apiStack[0]);
    size_t index = jsrCheckIndex(vm, 1, l->count, "i");
    if(index == SIZE_MAX) return false;

    listInsert(vm, l, index, vm->apiStack[2]);
    jsrPushNull(vm);
    return true;
}

JSR_NATIVE(jsr_List_len) {
    push(vm, NUM_VAL(AS_LIST(vm->apiStack[0])->count));
    return true;
}

JSR_NATIVE(jsr_List_removeAt) {
    ObjList *l = AS_LIST(vm->apiStack[0]);
    size_t index = jsrCheckIndex(vm, 1, l->count, "i");
    if(index == SIZE_MAX) return false;
    
    Value r = l->arr[index];
    listRemove(vm, l, index);
    push(vm, r);
    return true;
}

JSR_NATIVE(jsr_List_subList) {
    ObjList *list = AS_LIST(vm->apiStack[0]);

    size_t from = jsrCheckIndex(vm, 1, list->count + 1, "from");
    if(from == SIZE_MAX) return false;
    size_t to = jsrCheckIndex(vm, 2, list->count + 1, "to");
    if(to == SIZE_MAX) return false;

    if(from > to) JSR_RAISE(vm, "InvalidArgException", "from must be <= to.");

    size_t numElems = to - from;
    ObjList *subList = newList(vm, numElems < 16 ? 16 : numElems);

    memcpy(subList->arr, list->arr + from, numElems * sizeof(Value));
    subList->count = numElems;

    push(vm, OBJ_VAL(subList));
    return true;
}

JSR_NATIVE(jsr_List_clear) {
    AS_LIST(vm->apiStack[0])->count = 0;
    jsrPushNull(vm);
    return true;
}

JSR_NATIVE(jsr_List_iter) {
    ObjList *lst = AS_LIST(vm->apiStack[0]);

    if(IS_NULL(vm->apiStack[1])) {
        if(lst->count == 0) {
            push(vm, BOOL_VAL(false));
            return true;
        }
        push(vm, NUM_VAL(0));
        return true;
    }

    if(IS_NUM(vm->apiStack[1])) {
        double idx = AS_NUM(vm->apiStack[1]);
        if(idx >= 0 && idx < lst->count - 1) {
            push(vm, NUM_VAL(idx + 1));
            return true;
        }
    }

    push(vm, BOOL_VAL(false));
    return true;
}

JSR_NATIVE(jsr_List_next) {
    ObjList *lst = AS_LIST(vm->apiStack[0]);

    if(IS_NUM(vm->apiStack[1])) {
        double idx = AS_NUM(vm->apiStack[1]);
        if(idx >= 0 && idx < lst->count) {
            push(vm, lst->arr[(size_t)idx]);
            return true;
        }
    }

    push(vm, NULL_VAL);
    return true;
}
// end

// class Tuple
JSR_NATIVE(jsr_Tuple_new) {
    if(!jsrIsList(vm, 1)) {
        jsrPushList(vm);
        JSR_FOREACH(1, {
                jsrListAppend(vm, 2);
                jsrPop(vm);
        },)
    }

    ObjList *lst = AS_LIST(vm->sp[-1]);
    ObjTuple *tup = newTuple(vm, lst->count);
    if(lst->count > 0) memcpy(tup->arr, lst->arr, sizeof(Value) * lst->count);
    push(vm, OBJ_VAL(tup));
    return true;
}

JSR_NATIVE(jsr_Tuple_len) {
    push(vm, NUM_VAL(AS_TUPLE(vm->apiStack[0])->size));
    return true;
}

JSR_NATIVE(jsr_Tuple_iter) {
    ObjTuple *tup = AS_TUPLE(vm->apiStack[0]);

    if(jsrIsNull(vm, 1)) {
        if(tup->size == 0) {
            push(vm, BOOL_VAL(false));
            return true;
        }
        push(vm, NUM_VAL(0));
        return true;
    }

    if(jsrIsNumber(vm, 1)) {
        double idx = AS_NUM(vm->apiStack[1]);
        if(idx >= 0 && idx < tup->size - 1) {
            push(vm, NUM_VAL(idx + 1));
            return true;
        }
    }

    push(vm, BOOL_VAL(false));
    return true;
}

JSR_NATIVE(jsr_Tuple_next) {
    ObjTuple *tup = AS_TUPLE(vm->apiStack[0]);

    if(IS_NUM(vm->apiStack[1])) {
        double idx = AS_NUM(vm->apiStack[1]);
        if(idx >= 0 && idx < tup->size) {
            push(vm, tup->arr[(size_t)idx]);
            return true;
        }
    }

    push(vm, NULL_VAL);
    return true;
}

JSR_NATIVE(jsr_Tuple_sub) {
    ObjTuple *tup = AS_TUPLE(vm->apiStack[0]);

    size_t from = jsrCheckIndex(vm, 1, tup->size, "from");
    if(from == SIZE_MAX) return false;
    size_t to = jsrCheckIndex(vm, 2, tup->size + 1, "to");
    if(to == SIZE_MAX) return false;

    if(from >= to) JSR_RAISE(vm, "InvalidArgException", "from must be < to.");

    size_t numElems = to - from;
    ObjTuple *sub = newTuple(vm, numElems);

    memcpy(sub->arr, tup->arr + from, numElems * sizeof(Value));

    push(vm, OBJ_VAL(sub));
    return true;
}
// end

// class String
JSR_NATIVE(jsr_substr) {
    ObjString *str = AS_STRING(vm->apiStack[0]);

    size_t from = jsrCheckIndex(vm, 1, str->length + 1, "from");
    if(from == SIZE_MAX) return false;
    size_t to = jsrCheckIndex(vm, 2, str->length + 1, "to");
    if(to == SIZE_MAX) return false;

    if(from > to) JSR_RAISE(vm, "InvalidArgException", "argument from must be <= to.");

    size_t len = to - from;
    ObjString *sub = allocateString(vm, len);
    memcpy(sub->data, str->data + from, len);

    push(vm, OBJ_VAL(sub));
    return true;
}

JSR_NATIVE(jsr_String_startsWith) {
    if(!jsrCheckStr(vm, 1, "prefix") || !jsrCheckInt(vm, 2, "offset")) {
        return false;
    }

    const char *prefix = jsrGetString(vm, 1);
    size_t prefixLen = jsrGetStringSz(vm, 1);
    int offset = jsrGetNumber(vm, 2);
    size_t thisLen = jsrGetStringSz(vm, 0);

    if(offset < 0 || thisLen < (size_t) offset || thisLen - offset < prefixLen) {
        jsrPushBoolean(vm, false);
        return true;
    }

    const char *thisStr = jsrGetString(vm, 0) + offset;
    if(memcmp(thisStr, prefix, prefixLen) == 0) {
        jsrPushBoolean(vm, true);
        return true;
    }

    jsrPushBoolean(vm, false);
    return true;
}

JSR_NATIVE(jsr_String_endsWith) {
    if(!jsrCheckStr(vm, 1, "suffix")) return false;

    const char *suffix = jsrGetString(vm, 1);
    size_t suffixLen = jsrGetStringSz(vm, 1);
    size_t thisLen = jsrGetStringSz(vm, 0);

    if(thisLen < suffixLen) {
        jsrPushBoolean(vm, false);
        return true;
    }

    const char *thisStr = jsrGetString(vm, 0) + (thisLen - suffixLen);

    if(memcmp(thisStr, suffix, suffixLen) == 0) {
        jsrPushBoolean(vm, true);
        return true;
    }

    jsrPushBoolean(vm, false);
    return true;
}

JSR_NATIVE(jsr_String_join) {
    JStarBuffer joined;
    jsrBufferInit(vm, &joined);

    JSR_FOREACH(1, {
            if(!jsrIsString(vm, -1)) {
                if(jsrCallMethod(vm, "__string__", 0) != VM_EVAL_SUCCESS) {
                    jsrBufferFree(&joined);
                    return false;
                }
            }
            jsrBufferAppend(&joined, jsrGetString(vm, -1), jsrGetStringSz(vm, -1));
            jsrBufferAppend(&joined, jsrGetString(vm, 0), jsrGetStringSz(vm, 0));
            jsrPop(vm);
    }, jsrBufferFree(&joined))

    if(joined.len > 0) {
        jsrBufferTrunc(&joined, joined.len - jsrGetStringSz(vm, 0));
    }

    jsrBufferPush(&joined);
    return true;
}

JSR_NATIVE(jsr_String_len) {
    jsrPushNumber(vm, jsrGetStringSz(vm, 0));
    return true;
}

JSR_NATIVE(jsr_String_string) {
    return true;
}

JSR_NATIVE(jsr_String_hash) {
    jsrPushNumber(vm, STRING_GET_HASH(AS_STRING(vm->apiStack[0])));
    return true;
}

JSR_NATIVE(jsr_String_eq) {
    if(!jsrIsString(vm, 1)) {
        jsrPushBoolean(vm, false);
        return true;
    }

    ObjString *s1 = AS_STRING(vm->apiStack[0]);
    ObjString *s2 = AS_STRING(vm->apiStack[1]);

    if(s1->interned && s2->interned) {
        jsrPushBoolean(vm, s1 == s2);
        return true;
    }

    if(s1->length != s2->length) {
        jsrPushBoolean(vm, false);
        return true;
    }

    jsrPushBoolean(vm, memcmp(s1->data, s2->data, s1->length) == 0);
    return true;
}

JSR_NATIVE(jsr_String_iter) {
    ObjString *s = AS_STRING(vm->apiStack[0]);

    if(IS_NULL(vm->apiStack[1])) {
        if(s->length == 0) {
            push(vm, BOOL_VAL(false));
            return true;
        }
        push(vm, NUM_VAL(0));
        return true;
    }

    if(IS_NUM(vm->apiStack[1])) {
        double idx = AS_NUM(vm->apiStack[1]);
        if(idx >= 0 && idx < s->length - 1) {
            push(vm, NUM_VAL(idx + 1));
            return true;
        }
    }

    push(vm, BOOL_VAL(false));
    return true;
}

JSR_NATIVE(jsr_String_next) {
    ObjString *str = AS_STRING(vm->apiStack[0]);

    if(IS_NUM(vm->apiStack[1])) {
        double idx = AS_NUM(vm->apiStack[1]);
        if(idx >= 0 && idx < str->length) {
            jsrPushStringSz(vm, str->data + (size_t)idx, 1);
            return true;
        }
    }

    push(vm, NULL_VAL);
    return true;
}
// end 

// class Table
#define MAX_LOAD_FACTOR  0.75
#define GROW_FACTOR      2
#define INITIAL_CAPACITY 8

static bool findEntry(JStarVM *vm, TableEntry *entries, size_t sizeMask, Value key, TableEntry **out) {
    push(vm, key);
    if(jsrCallMethod(vm, "__hash__", 0) != VM_EVAL_SUCCESS) return false;
    if(!jsrCheckNum(vm, -1, "__hash__ return value")) return false;

    uint32_t hash = jsrGetNumber(vm, -1);
    pop(vm);

    size_t i = hash & sizeMask;
    TableEntry *tomb = NULL;

    for(;;) {
        TableEntry *e = &entries[i];
        if(IS_NULL(e->key)) {
            if(IS_NULL(e->val)) {
                if(tomb)
                    *out = tomb;
                else
                    *out = e;
                return true;
            } else if(!tomb) {
                tomb = e;
            }
        } else {
            push(vm, key);
            push(vm, e->key);
            if(jsrCallMethod(vm, "__eq__", 1) != VM_EVAL_SUCCESS) return false;
            if(!jsrCheckBool(vm, -1, "__eq__ return value")) return false;

            bool equals = jsrGetBoolean(vm, -1);
            pop(vm);

            if(equals) {
                *out = e;
                return true;
            }
        }
        i = (i + 1) & sizeMask;
    }
}

static void growEntries(JStarVM *vm, ObjTable *t) {
    size_t newSize = t->sizeMask ? (t->sizeMask + 1) * GROW_FACTOR : INITIAL_CAPACITY;
    TableEntry *newEntries = GC_ALLOC(vm, sizeof(TableEntry) * newSize);
    for(size_t i = 0; i < newSize; i++) {
        newEntries[i].key = NULL_VAL;
        newEntries[i].val = NULL_VAL;
    }

    t->numEntries = 0, t->count = 0;
    if(t->sizeMask != 0) {
        for(size_t i = 0; i <= t->sizeMask; i++) {
            TableEntry *e = &t->entries[i];
            if(IS_NULL(e->key)) continue;

            TableEntry *dest;
            findEntry(vm, newEntries, newSize - 1, e->key, &dest);
            dest->key = e->key;
            dest->val = e->val;
            t->numEntries++, t->count++;
        }
        GC_FREEARRAY(vm, TableEntry, t->entries, t->sizeMask + 1);
    }
    t->entries = newEntries;
    t->sizeMask = newSize - 1;
}

JSR_NATIVE(jsr_Table_get) {
    if(jsrIsNull(vm, 1)) JSR_RAISE(vm, "TypeException", "Key of Table cannot be null.");
    
    ObjTable *t = AS_TABLE(vm->apiStack[0]);
    if(t->entries == NULL) {
        push(vm, NULL_VAL);
        return true;
    }

    TableEntry *e;
    if(!findEntry(vm, t->entries, t->sizeMask, vm->apiStack[1], &e)) {
        return false;
    }

    if(!IS_NULL(e->key))
        push(vm, e->val);
    else
        push(vm, NULL_VAL);

    return true;
}

JSR_NATIVE(jsr_Table_set) {
    if(jsrIsNull(vm, 1)) JSR_RAISE(vm, "TypeException", "Key of Table cannot be null.");
    
    ObjTable *t = AS_TABLE(vm->apiStack[0]);
    if(t->numEntries + 1 > (t->sizeMask + 1) * MAX_LOAD_FACTOR) {
        growEntries(vm, t);
    }

    TableEntry *e;
    if(!findEntry(vm, t->entries, t->sizeMask, vm->apiStack[1], &e)) {
        return false;
    }
    bool isNew = IS_NULL(e->key);
    if(isNew) {
        t->count++;
        if(IS_NULL(e->val)) t->numEntries++;
    }

    e->key = vm->apiStack[1];
    e->val = vm->apiStack[2];

    push(vm, BOOL_VAL(isNew));
    return true;
}

JSR_NATIVE(jsr_Table_delete) {
    if(jsrIsNull(vm, 1)) JSR_RAISE(vm, "TypeException", "Key of Table cannot be null.");
    ObjTable *t = AS_TABLE(vm->apiStack[0]);
    
    if(t->entries == NULL) {
        push(vm, BOOL_VAL(false));
        return true;
    }

    TableEntry *toDelete;
    if(!findEntry(vm, t->entries, t->sizeMask, vm->apiStack[1], &toDelete)) {
        return false;
    }

    if(IS_NULL(toDelete->key)) {
        jsrPushBoolean(vm, false);
        return true;
    }

    toDelete->key = NULL_VAL;
    toDelete->val = TRUE_VAL;
    t->count--;

    push(vm, BOOL_VAL(true));
    return true;
}

JSR_NATIVE(jsr_Table_clear) {
    ObjTable *t = AS_TABLE(vm->apiStack[0]);
    t->numEntries = t->count = 0;
    for(size_t i = 0; i < t->sizeMask + 1; i++) {
        t->entries[i].key = NULL_VAL;
        t->entries[i].val = NULL_VAL;
    }
    push(vm, NULL_VAL);
    return true;
}

JSR_NATIVE(jsr_Table_len) {
    ObjTable *t = AS_TABLE(vm->apiStack[0]);
    push(vm, NUM_VAL(t->count));
    return true;    
}

JSR_NATIVE(jsr_Table_contains) {
    if(jsrIsNull(vm, 0)) JSR_RAISE(vm, "TypeException", "Key of Table cannot be null.");
    
    ObjTable *t = AS_TABLE(vm->apiStack[0]);
    if(t->entries == NULL) {
        push(vm, BOOL_VAL(false));
        return true;
    }

    TableEntry *e;
    if(!findEntry(vm, t->entries, t->sizeMask, vm->apiStack[1], &e)) {
        return false;
    }

    push(vm, BOOL_VAL(!IS_NULL(e->key)));
    return true;
}

JSR_NATIVE(jsr_Table_keys) {
    ObjTable *t = AS_TABLE(vm->apiStack[0]);
    TableEntry *entries = t->entries;

    jsrPushList(vm);

    if(entries != NULL) {
        for(size_t i = 0; i < t->sizeMask + 1; i++) {
            if(!IS_NULL(entries[i].key)) {
                push(vm, entries[i].key);
                jsrListAppend(vm, -2);
                jsrPop(vm);
            }
        }
    }

    return true;
}

JSR_NATIVE(jsr_Table_values) {
    ObjTable *t = AS_TABLE(vm->apiStack[0]);
    TableEntry *entries = t->entries;

    jsrPushList(vm);

    if(entries != NULL) {
        for(size_t i = 0; i < t->sizeMask + 1; i++) {
            if(!IS_NULL(entries[i].key)) {
                push(vm, entries[i].val);
                jsrListAppend(vm, -2);
                jsrPop(vm);
            }
        }
    }

    return true;
}

JSR_NATIVE(jsr_Table_iter) {
    ObjTable *t = AS_TABLE(vm->apiStack[0]);

    if(IS_NULL(vm->apiStack[1]) && t->entries == NULL) {
        push(vm, BOOL_VAL(false));
        return true;
    }

    size_t lastIdx = 0;

    if(IS_NUM(vm->apiStack[1])) {
        double idx = AS_NUM(vm->apiStack[1]);
        if(idx < 0 && idx >= t->sizeMask) {
            push(vm, BOOL_VAL(false));
            return true;
        }
        lastIdx = (size_t)idx + 1;
    }

    for(size_t i = lastIdx; i < t->sizeMask + 1; i++) {
        if(!IS_NULL(t->entries[i].key)) {
            push(vm, NUM_VAL(i));
            return true;
        }
    }

    push(vm, BOOL_VAL(false));
    return true;
}

JSR_NATIVE(jsr_Table_next) {
    ObjTable *t = AS_TABLE(vm->apiStack[0]);

    if(IS_NUM(vm->apiStack[1])) {
        double idx = AS_NUM(vm->apiStack[1]);
        if(idx >= 0 && idx <= t->sizeMask) {
            size_t i = (size_t)idx;
            push(vm, t->entries[i].key);
            return true;
        }
    }

    push(vm, NULL_VAL);
    return true;
}

JSR_NATIVE(jsr_Table_string) {
    ObjTable *t = AS_TABLE(vm->apiStack[0]);
    
    JStarBuffer buf;
    jsrBufferInit(vm, &buf);
    jsrBufferAppendChar(&buf, '{');

    TableEntry *entries = t->entries;
    if(entries != NULL) {
        for(size_t i = 0; i < t->sizeMask + 1; i++) {
            if(!IS_NULL(entries[i].key)) {
                push(vm, entries[i].key);
                if(jsrCallMethod(vm, "__string__", 0) != VM_EVAL_SUCCESS || !jsrIsString(vm, -1)) {
                    jsrBufferFree(&buf);
                    return false;
                }
                jsrBufferAppendstr(&buf, jsrGetString(vm, -1));
                jsrBufferAppendstr(&buf, " : ");
                jsrPop(vm);

                push(vm, entries[i].val);
                if(jsrCallMethod(vm, "__string__", 0) != VM_EVAL_SUCCESS || !jsrIsString(vm, -1)) {
                    jsrBufferFree(&buf);
                    return false;
                }
                jsrBufferAppendstr(&buf, jsrGetString(vm, -1));
                jsrBufferAppendstr(&buf, ", ");
                jsrPop(vm);
            }
        }
        jsrBufferTrunc(&buf, buf.len - 2);
    }
    jsrBufferAppendChar(&buf, '}');
    jsrBufferPush(&buf);
    return true;
}
// end
