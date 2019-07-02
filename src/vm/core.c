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

static ObjClass *createClass(BlangVM *vm, ObjModule *m, ObjClass *sup, const char *name) {
    ObjString *n = copyString(vm, name, strlen(name), true);
    push(vm, OBJ_VAL(n));
    ObjClass *c = newClass(vm, n, sup);
    pop(vm);

    hashTablePut(&m->globals, n, OBJ_VAL(c));
    return c;
}

static Value getDefinedName(BlangVM *vm, ObjModule *m, const char *name) {
    Value v = NULL_VAL;
    hashTableGet(&m->globals, copyString(vm, name, strlen(name), true), &v);
    return v;
}

static void defMethod(BlangVM *vm, ObjModule *m, ObjClass *cls, Native n, const char *name,
                      uint8_t argc) {
    ObjString *strName = copyString(vm, name, strlen(name), true);
    push(vm, OBJ_VAL(strName));
    ObjNative *native = newNative(vm, m, strName, argc, n, 0);
    pop(vm);

    hashTablePut(&cls->methods, strName, OBJ_VAL(native));
}

static void defMethodDefaults(BlangVM *vm, ObjModule *m, ObjClass *cls, Native n, const char *name,
                              uint8_t argc, uint8_t defc, ...) {
    ObjString *strName = copyString(vm, name, strlen(name), true);
    push(vm, OBJ_VAL(strName));

    ObjNative *native = newNative(vm, m, strName, argc, n, defc);

    va_list args;
    va_start(args, defc);
    for(size_t i = 0; i < defc; i++) {
        native->c.defaults[i] = va_arg(args, Value);
    }
    va_end(args);
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
static NATIVE(bl_Object_string) {
    Obj *o = AS_OBJ(vm->apiStack[0]);
    char str[256];
    snprintf(str, 255, "<%s@%p>", o->cls->name->data, (void *)o);
    blPushString(vm, str);
    return true;
}

static NATIVE(bl_Object_hash) {
    uint64_t x = hash64((uint64_t)AS_OBJ(vm->apiStack[0]));
    blPushNumber(vm, (uint32_t)x);
    return true;
}
// Object

// class Class
static NATIVE(bl_Class_getName) {
    push(vm, OBJ_VAL(AS_CLASS(vm->apiStack[0])->name));
    return true;
}

static NATIVE(bl_Class_string) {
    Obj *o = AS_OBJ(vm->apiStack[0]);
    char str[256];
    snprintf(str, 255, "<Class %s@%p>", ((ObjClass *)o)->name->data, (void *)o);
    blPushString(vm, str);
    return true;
}
// Class

void initCoreLibrary(BlangVM *vm) {
    ObjString *name = copyString(vm, CORE_MODULE, strlen(CORE_MODULE), true);

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
    defMethod(vm, core, vm->objClass, &bl_Object_string, "__string__", 0);
    defMethod(vm, core, vm->objClass, &bl_Object_hash, "__hash__", 0);

    // Patch up Class object information
    vm->clsClass->superCls = vm->objClass;
    hashTableMerge(&vm->clsClass->methods, &vm->objClass->methods);
    defMethod(vm, core, vm->clsClass, &bl_Class_getName, "getName", 0);
    defMethod(vm, core, vm->clsClass, &bl_Class_string, "__string__", 0);

    blEvaluateModule(vm, "__core__", "__core__", readBuiltInModule("__core__"));

    vm->strClass = AS_CLASS(getDefinedName(vm, core, "String"));
    vm->boolClass = AS_CLASS(getDefinedName(vm, core, "Boolean"));
    vm->lstClass = AS_CLASS(getDefinedName(vm, core, "List"));
    vm->numClass = AS_CLASS(getDefinedName(vm, core, "Number"));
    vm->funClass = AS_CLASS(getDefinedName(vm, core, "Function"));
    vm->modClass = AS_CLASS(getDefinedName(vm, core, "Module"));
    vm->nullClass = AS_CLASS(getDefinedName(vm, core, "Null"));
    vm->stClass = AS_CLASS(getDefinedName(vm, core, "StackTrace"));
    vm->tupClass = AS_CLASS(getDefinedName(vm, core, "Tuple"));

    core->base.cls = vm->modClass;

    // Set constructor for instatiable primitive classes
    defMethodDefaults(vm, core, vm->lstClass, &bl_List_new, "new", 2, 2, NUM_VAL(0), NULL_VAL);
    defMethod(vm, core, vm->tupClass, &bl_Tuple_new, "new", 1);

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

NATIVE(bl_int) {
    if(blIsNumber(vm, 1)) {
        blPushNumber(vm, trunc(blGetNumber(vm, 1)));
        return true;
    }
    if(blIsString(vm, 1)) {
        char *end = NULL;
        const char *nstr = blGetString(vm, 1);
        long long n = strtoll(nstr, &end, 10);

        if((n == 0 && end == nstr) || *end != '\0') {
            BL_RAISE(vm, "InvalidArgException", "\"%s\".", nstr);
        }
        if(n == LLONG_MAX) {
            BL_RAISE(vm, "InvalidArgException", "Overflow: \"%s\".", nstr);
        }
        if(n == LLONG_MIN) {
            BL_RAISE(vm, "InvalidArgException", "Underflow: \"%s\".", nstr);
        }

        blPushNumber(vm, n);
        return true;
    }

    BL_RAISE(vm, "InvalidArgException", "Argument must be a number or a string.");
}

NATIVE(bl_num) {
    if(blIsNumber(vm, 1)) {
        blPushNumber(vm, blGetNumber(vm, 1));
        return true;
    }
    if(blIsString(vm, 1)) {
        errno = 0;

        char *end = NULL;
        const char *nstr = blGetString(vm, 1);
        double n = strtod(nstr, &end);

        if((n == 0 && end == nstr) || *end != '\0') {
            BL_RAISE(vm, "InvalidArgException", "\"%s\".", nstr);
        }
        if(n == HUGE_VAL || n == -HUGE_VAL) {
            BL_RAISE(vm, "InvalidArgException", "Overflow: \"%s\".", nstr);
        }
        if(n == 0 && errno == ERANGE) {
            BL_RAISE(vm, "InvalidArgException", "Underflow: \"%s\".", nstr);
        }

        blPushNumber(vm, n);
        return true;
    }

    BL_RAISE(vm, "InvalidArgException", "Argument must be a number or a string.");
}

NATIVE(bl_isInt) {
    if(blIsNumber(vm, 1)) {
        double n = blGetNumber(vm, 1);
        blPushBoolean(vm, trunc(n) == n);
        return true;
    }
    blPushBoolean(vm, false);
    return true;
}

NATIVE(bl_char) {
    if(!blCheckInt(vm, 1, "num")) return false;
    char c = blGetNumber(vm, 1);
    blPushStringSz(vm, &c, 1);
    return true;
}

NATIVE(bl_ascii) {
    if(!blCheckStr(vm, 1, "arg")) return false;

    const char *str = blGetString(vm, 1);
    if(strlen(str) != 1) BL_RAISE(vm, "InvalidArgException", "arg must be a String of length 1");

    char c = str[0];
    blPushNumber(vm, (double)c);
    return true;
}

NATIVE(bl_printstr) {
    if(!blCheckStr(vm, 1, "str")) return false;
    fwrite(blGetString(vm, 1), 1, blGetStringSz(vm, 1), stdout);
    blPushNull(vm);
    return true;
}

NATIVE(bl_eval) {
    if(!blCheckStr(vm, 1, "source")) return false;
    if(vm->frameCount < 1) {
        BL_RAISE(vm, "Exception", "eval() can only be called by another function");
    }
    Frame *prevFrame = &vm->frames[vm->frameCount - 2];
    ObjModule *mod = prevFrame->fn.type == OBJ_CLOSURE ? prevFrame->fn.closure->fn->c.module
                                                       : prevFrame->fn.native->c.module;

    EvalResult res = blEvaluateModule(vm, "<string>", mod->name->data, blGetString(vm, 1));
    blPushBoolean(vm, res == VM_EVAL_SUCCSESS);
    return true;
}

NATIVE(bl_type) {
    push(vm, OBJ_VAL(getClass(vm, peek(vm))));
    return true;
}

// class Number {
NATIVE(bl_Number_string) {
    char str[24]; // enough for .*g with DBL_DIG
    snprintf(str, sizeof(str) - 1, "%.*g", DBL_DIG, blGetNumber(vm, 0));
    blPushString(vm, str);
    return true;
}

NATIVE(bl_Number_hash) {
    double num = blGetNumber(vm, 0);
    if(num == 0) num = 0;
    union {
        double d;
        uint64_t r;
    } c = {.d = num};
    uint64_t n = hash64(c.r);
    blPushNumber(vm, (uint32_t)n);
    return true;
}
// } Number

// class Boolean {
NATIVE(bl_Boolean_string) {
    if(blGetBoolean(vm, 0))
        blPushString(vm, "true");
    else
        blPushString(vm, "false");
    return true;
}
// } Boolean

// class Null {
NATIVE(bl_Null_string) {
    blPushString(vm, "null");
    return true;
}
// } Null

// class Function {
NATIVE(bl_Function_string) {
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
        ObjBoundMethod *m = AS_BOUND_METHOD(vm->apiStack[0]);
        funType = "bound method";
        funName = m->method->type == OBJ_CLOSURE ? ((ObjClosure *)m->method)->fn->c.name->data
                                                 : ((ObjNative *)m->method)->c.name->data;
        modName = m->method->type == OBJ_CLOSURE
                      ? ((ObjClosure *)m->method)->fn->c.module->name->data
                      : ((ObjNative *)m->method)->c.module->name->data;
        break;
    }
    default:
        break;
    }

    char str[512] = {0};
    snprintf(str, sizeof(str) - 1, "<%s %s.%s@%p>", funType, modName, funName,
             AS_OBJ(vm->apiStack[0]));

    blPushString(vm, str);
    return true;
}
// } Function

// class Module {
NATIVE(bl_Module_string) {
    char str[256];
    ObjModule *m = AS_MODULE(vm->apiStack[0]);
    snprintf(str, sizeof(str) - 1, "<module %s@%p>", m->name->data, m);
    blPushString(vm, str);
    return true;
}
// } Module

// class List {
NATIVE(bl_List_new) {
    if(!blCheckInt(vm, 1, "size")) return false;
    double count = blGetNumber(vm, 1);

    if(count < 0) BL_RAISE(vm, "TypeException", "size must be >= 0");
    ObjList *lst = newList(vm, count < 16 ? 16 : count);
    lst->count = count;
    push(vm, OBJ_VAL(lst));

    if(IS_CLOSURE(vm->apiStack[2]) || IS_NATIVE(vm->apiStack[2])) {
        for(size_t i = 0; i < lst->count; i++) {
            blPushValue(vm, 2);
            blPushNumber(vm, i);
            if(blCall(vm, 1) != VM_EVAL_SUCCSESS) return false;
            lst->arr[i] = pop(vm);
        }
    } else {
        for(size_t i = 0; i < lst->count; i++) {
            lst->arr[i] = vm->apiStack[2];
        }
    }

    return true;
}

NATIVE(bl_List_add) {
    ObjList *l = AS_LIST(vm->apiStack[0]);
    listAppend(vm, l, vm->apiStack[1]);
    blPushNull(vm);
    return true;
}

NATIVE(bl_List_insert) {
    ObjList *l = AS_LIST(vm->apiStack[0]);
    size_t index = blCheckIndex(vm, 1, l->count, "i");
    if(index == SIZE_MAX) return false;

    listInsert(vm, l, index, vm->apiStack[2]);
    blPushNull(vm);
    return true;
}

NATIVE(bl_List_len) {
    push(vm, NUM_VAL(AS_LIST(vm->apiStack[0])->count));
    return true;
}

NATIVE(bl_List_removeAt) {
    ObjList *l = AS_LIST(vm->apiStack[0]);
    size_t index = blCheckIndex(vm, 1, l->count, "i");
    if(index == SIZE_MAX) return false;
    
    Value r = l->arr[index];
    listRemove(vm, l, index);
    push(vm, r);
    return true;
}

NATIVE(bl_List_subList) {
    ObjList *list = AS_LIST(vm->apiStack[0]);

    size_t from = blCheckIndex(vm, 1, list->count, "from");
    if(from == SIZE_MAX) return false;
    size_t to = blCheckIndex(vm, 2, list->count + 1, "to");
    if(to == SIZE_MAX) return false;

    if(from >= to) BL_RAISE(vm, "InvalidArgException", "from must be < to.");

    size_t numElems = to - from;
    ObjList *subList = newList(vm, numElems < 16 ? 16 : numElems);

    memcpy(subList->arr, list->arr + from, numElems * sizeof(Value));
    subList->count = numElems;

    push(vm, OBJ_VAL(subList));
    return true;
}

NATIVE(bl_List_clear) {
    AS_LIST(vm->apiStack[0])->count = 0;
    blPushNull(vm);
    return true;
}

NATIVE(bl_List_iter) {
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

NATIVE(bl_List_next) {
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
// } List

// class Tuple {
NATIVE(bl_Tuple_new) {
    if(!blIsList(vm, 1)) {
        blPushList(vm);
        blForEach(1, {
                blListAppend(vm, 2);
                blPop(vm);
        },)
    }

    ObjList *lst = AS_LIST(vm->sp[-1]);
    ObjTuple *tup = newTuple(vm, lst->count);
    if(lst->count > 0) memcpy(tup->arr, lst->arr, sizeof(Value) * lst->count);
    push(vm, OBJ_VAL(tup));
    return true;
}

NATIVE(bl_Tuple_len) {
    push(vm, NUM_VAL(AS_TUPLE(vm->apiStack[0])->size));
    return true;
}

NATIVE(bl_Tuple_iter) {
    ObjTuple *tup = AS_TUPLE(vm->apiStack[0]);

    if(blIsNull(vm, 1)) {
        if(tup->size == 0) {
            push(vm, BOOL_VAL(false));
            return true;
        }
        push(vm, NUM_VAL(0));
        return true;
    }

    if(blIsNumber(vm, 1)) {
        double idx = AS_NUM(vm->apiStack[1]);
        if(idx >= 0 && idx < tup->size - 1) {
            push(vm, NUM_VAL(idx + 1));
            return true;
        }
    }

    push(vm, BOOL_VAL(false));
    return true;
}

NATIVE(bl_Tuple_next) {
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

NATIVE(bl_Tuple_sub) {
    ObjTuple *tup = AS_TUPLE(vm->apiStack[0]);

    size_t from = blCheckIndex(vm, 1, tup->size, "from");
    if(from == SIZE_MAX) return false;
    size_t to = blCheckIndex(vm, 2, tup->size + 1, "to");
    if(to == SIZE_MAX) return false;

    if(from >= to) BL_RAISE(vm, "InvalidArgException", "from must be < to.");

    size_t numElems = to - from;
    ObjTuple *sub = newTuple(vm, numElems);

    memcpy(sub->arr, tup->arr + from, numElems * sizeof(Value));

    push(vm, OBJ_VAL(sub));
    return true;
}
// }

// class String {
NATIVE(bl_substr) {
    ObjString *str = AS_STRING(vm->apiStack[0]);

    size_t from = blCheckIndex(vm, 1, str->length + 1, "from");
    if(from == SIZE_MAX) return false;
    size_t to = blCheckIndex(vm, 2, str->length + 1, "to");
    if(to == SIZE_MAX) return false;

    if(from > to) BL_RAISE(vm, "InvalidArgException", "argument to must be >= from.");

    size_t len = to - from;
    ObjString *sub = allocateString(vm, len);
    memcpy(sub->data, str->data + from, len);

    push(vm, OBJ_VAL(sub));
    return true;
}

NATIVE(bl_String_join) {
    BlBuffer joined;
    blBufferInit(vm, &joined);

    blForEach(1, {
            if(!blIsString(vm, -1)) {
                if(blCallMethod(vm, "__string__", 0) != VM_EVAL_SUCCSESS) {
                    blBufferFree(&joined);
                    return false;
                }
            }
            blBufferAppend(&joined, blGetString(vm, -1), blGetStringSz(vm, -1));
            blBufferAppend(&joined, blGetString(vm, 0), blGetStringSz(vm, 0));
            blPop(vm);
    }, blBufferFree(&joined))

    if(joined.len > 0) {
        blBufferTrunc(&joined, joined.len - blGetStringSz(vm, 0));
    }

    blBufferPush(&joined);
    return true;
}

NATIVE(bl_String_len) {
    blPushNumber(vm, blGetStringSz(vm, 0));
    return true;
}

NATIVE(bl_String_string) {
    return true;
}

NATIVE(bl_String_hash) {
    blPushNumber(vm, STRING_GET_HASH(AS_STRING(vm->apiStack[0])));
    return true;
}

NATIVE(bl_String_eq) {
    if(!blIsString(vm, 1)) {
        blPushBoolean(vm, false);
        return true;
    }

    ObjString *s1 = AS_STRING(vm->apiStack[0]);
    ObjString *s2 = AS_STRING(vm->apiStack[1]);

    if(s1->interned && s2->interned) {
        blPushBoolean(vm, s1 == s2);
        return true;
    }

    if(s1->length != s2->length) {
        blPushBoolean(vm, false);
        return true;
    }

    blPushBoolean(vm, memcmp(s1->data, s2->data, s1->length) == 0);
    return true;
}

NATIVE(bl_String_iter) {
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

NATIVE(bl_String_next) {
    ObjString *str = AS_STRING(vm->apiStack[0]);

    if(IS_NUM(vm->apiStack[1])) {
        double idx = AS_NUM(vm->apiStack[1]);
        if(idx >= 0 && idx < str->length) {
            blPushStringSz(vm, str->data + (size_t)idx, 1);
            return true;
        }
    }

    push(vm, NULL_VAL);
    return true;
}
// } String
