#include "core.h"

#include <ctype.h>
#include <errno.h>
#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "const.h"
#include "gc.h"
#include "hashtable.h"
#include "import.h"
#include "modules.h"
#include "object.h"
#include "parse/ast.h"
#include "parse/parser.h"
#include "util.h"
#include "value.h"
#include "vm.h"

static ObjClass* createClass(JStarVM* vm, ObjModule* m, ObjClass* sup, const char* name) {
    ObjString* n = copyString(vm, name, strlen(name));
    push(vm, OBJ_VAL(n));
    ObjClass* c = newClass(vm, n, sup);
    pop(vm);
    hashTablePut(&m->globals, n, OBJ_VAL(c));
    return c;
}

static Value getDefinedName(JStarVM* vm, ObjModule* m, const char* name) {
    Value v = NULL_VAL;
    hashTableGet(&m->globals, copyString(vm, name, strlen(name)), &v);
    return v;
}

static void defMethod(JStarVM* vm, ObjModule* m, ObjClass* cls, JStarNative nat, const char* name,
                      uint8_t argc) {
    ObjString* strName = copyString(vm, name, strlen(name));
    push(vm, OBJ_VAL(strName));
    ObjNative* native = newNative(vm, m, argc, 0, false);
    native->c.name = strName;
    native->fn = nat;
    pop(vm);
    hashTablePut(&cls->methods, strName, OBJ_VAL(native));
}

static uint64_t hash64(uint64_t x) {
    x = (x ^ (x >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
    x = (x ^ (x >> 27)) * UINT64_C(0x94d049bb133111eb);
    x = x ^ (x >> 31);
    return x;
}

static uint32_t hashNumber(double num) {
    if(num == -0) num = 0;
    union {
        double d;
        uint64_t r;
    } c = {.d = num};
    return (uint32_t)hash64(c.r);
}

static bool compareValues(JStarVM* vm, const Value* v1, const Value* v2, size_t size, bool* out) {
    *out = true;
    for(size_t i = 0; i < size; i++) {
        push(vm, v1[i]);
        push(vm, v2[i]);

        if(jsrCallMethod(vm, "__eq__", 1) != JSR_SUCCESS) return false;

        bool res = valueToBool(pop(vm));
        if(!res) {
            *out = false;
            return true;
        }
    }
    return true;
}

// -----------------------------------------------------------------------------
// CLASS AND OBJECT CLASSES AND CORE MODULE INITIALIZATION
// -----------------------------------------------------------------------------

// class Object
static JSR_NATIVE(jsr_Object_string) {
    Obj* o = AS_OBJ(vm->apiStack[0]);
    JStarBuffer str;
    jsrBufferInit(vm, &str);
    jsrBufferAppendf(&str, "<%s@%p>", o->cls->name->data, (void*)o);
    jsrBufferPush(&str);
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
    Obj* o = AS_OBJ(vm->apiStack[0]);
    JStarBuffer str;
    jsrBufferInit(vm, &str);
    jsrBufferAppendf(&str, "<Class %s@%p>", ((ObjClass*)o)->name->data, (void*)o);
    jsrBufferPush(&str);
    return true;
}
// end

// Patch up the class field of any string or function that was allocated
// before the creation of their corresponding class object
static void patchClassRefs(JStarVM* vm) {
    for(Obj* o = vm->objects; o != NULL; o = o->next) {
        if(o->type == OBJ_STRING) {
            o->cls = vm->strClass;
        } else if(o->type == OBJ_CLOSURE || o->type == OBJ_FUNCTION || o->type == OBJ_NATIVE) {
            o->cls = vm->funClass;
        }
    }
}

static void createArgvList(JStarVM* vm) {
    vm->argv = newList(vm, 0);
    ObjString* argvName = copyString(vm, ARGV_STR, strlen(ARGV_STR));
    hashTablePut(&vm->core->globals, argvName, OBJ_VAL(vm->argv));
}

void initCoreModule(JStarVM* vm) {
    // Create and register core module
    ObjString* coreModName = copyString(vm, JSR_CORE_MODULE, strlen(JSR_CORE_MODULE));
    push(vm, OBJ_VAL(coreModName));

    ObjModule* core = newModule(vm, coreModName);
    setModule(vm, core->name, core);
    vm->core = core;

    pop(vm);

    // Setup the class object. It will be the class of every other class
    vm->clsClass = createClass(vm, core, NULL, "Class");
    vm->clsClass->base.cls = vm->clsClass;  // Class is the class of itself

    // Setup the base class of the object hierarchy
    vm->objClass = createClass(vm, core, NULL, "Object");  // Object has no superclass
    defMethod(vm, core, vm->objClass, &jsr_Object_string, "__string__", 0);
    defMethod(vm, core, vm->objClass, &jsr_Object_hash, "__hash__", 0);
    defMethod(vm, core, vm->objClass, &jsr_Object_eq, "__eq__", 1);

    // Patch up Class object information
    vm->clsClass->superCls = vm->objClass;
    hashTableMerge(&vm->clsClass->methods, &vm->objClass->methods);
    defMethod(vm, core, vm->clsClass, &jsr_Class_getName, "getName", 0);
    defMethod(vm, core, vm->clsClass, &jsr_Class_string, "__string__", 0);

    // Read core module
    size_t len;
    const char* coreBytecode = readBuiltInModule(JSR_CORE_MODULE, &len);

    // Execute core module
    JStarBuffer code = jsrBufferWrap(vm, coreBytecode, len);
    jsrEvalModule(vm, JSR_CORE_MODULE, JSR_CORE_MODULE, &code);

    // Cache builtin class objects in JStarVM
    vm->strClass = AS_CLASS(getDefinedName(vm, core, "String"));
    vm->boolClass = AS_CLASS(getDefinedName(vm, core, "Boolean"));
    vm->lstClass = AS_CLASS(getDefinedName(vm, core, "List"));
    vm->numClass = AS_CLASS(getDefinedName(vm, core, "Number"));
    vm->funClass = AS_CLASS(getDefinedName(vm, core, "Function"));
    vm->modClass = AS_CLASS(getDefinedName(vm, core, "Module"));
    vm->nullClass = AS_CLASS(getDefinedName(vm, core, "Null"));
    vm->stClass = AS_CLASS(getDefinedName(vm, core, "StackTrace"));
    vm->tupClass = AS_CLASS(getDefinedName(vm, core, "Tuple"));
    vm->excClass = AS_CLASS(getDefinedName(vm, core, "Exception"));
    vm->tableClass = AS_CLASS(getDefinedName(vm, core, "Table"));
    vm->udataClass = AS_CLASS(getDefinedName(vm, core, "Userdata"));
    core->base.cls = vm->modClass;

    // Call these after builtin class caching above, as they make use of those fields
    patchClassRefs(vm);
    createArgvList(vm);
}

// -----------------------------------------------------------------------------
// BUILTIN CLASSES
// -----------------------------------------------------------------------------

// class Number
JSR_NATIVE(jsr_Number_new) {
    if(jsrIsNumber(vm, 1)) {
        jsrPushNumber(vm, jsrGetNumber(vm, 1));
        return true;
    }
    if(jsrIsString(vm, 1)) {
        errno = 0;
        char* end = NULL;
        const char* nstr = jsrGetString(vm, 1);
        double n = strtod(nstr, &end);

        if((n == 0 && end == nstr) || *end != '\0') {
            JSR_RAISE(vm, "InvalidArgException", "'%s'.", nstr);
        }
        if(n == HUGE_VAL || n == -HUGE_VAL) {
            JSR_RAISE(vm, "InvalidArgException", "Overflow: '%s'.", nstr);
        }
        if(n == 0 && errno == ERANGE) {
            JSR_RAISE(vm, "InvalidArgException", "Underflow: '%s'.", nstr);
        }

        jsrPushNumber(vm, n);
        return true;
    }

    JSR_RAISE(vm, "TypeException", "n must be a Number or a String.");
}

JSR_NATIVE(jsr_Number_isInt) {
    double n = jsrGetNumber(vm, 0);
    jsrPushBoolean(vm, trunc(n) == n);
    return true;
}

JSR_NATIVE(jsr_Number_string) {
    char string[24];  // enough for .*g with DBL_DIG
    snprintf(string, sizeof(string), "%.*g", DBL_DIG, jsrGetNumber(vm, 0));
    jsrPushString(vm, string);
    return true;
}

JSR_NATIVE(jsr_Number_hash) {
    jsrPushNumber(vm, hashNumber(AS_NUM(vm->apiStack[0])));
    return true;
}
// end

// class Boolean
JSR_NATIVE(jsr_Boolean_new) {
    Value v = vm->apiStack[1];
    jsrPushBoolean(vm, valueToBool(v));
    return true;
}

JSR_NATIVE(jsr_Boolean_string) {
    jsrPushString(vm, AS_BOOL(vm->apiStack[0]) ? "true" : "false");
    return true;
}

JSR_NATIVE(jsr_Boolean_hash) {
    jsrPushNumber(vm, AS_BOOL(vm->apiStack[0]));
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
    const char* fnType = NULL;
    FnCommon* fn = NULL;

    Obj* obj = AS_OBJ(vm->apiStack[0]);
    switch(obj->type) {
    case OBJ_CLOSURE:
        fnType = "function";
        fn = &AS_CLOSURE(vm->apiStack[0])->fn->c;
        break;
    case OBJ_NATIVE:
        fnType = "native";
        fn = &AS_NATIVE(vm->apiStack[0])->c;
        break;
    case OBJ_BOUND_METHOD:
        fnType = "bound method";
        ObjBoundMethod* b = AS_BOUND_METHOD(vm->apiStack[0]);
        if(b->method->type == OBJ_CLOSURE) {
            fn = &((ObjClosure*)b->method)->fn->c;
        } else {
            fn = &((ObjNative*)b->method)->c;
        }
        break;
    default:
        UNREACHABLE();
        break;
    }

    JStarBuffer str;
    jsrBufferInit(vm, &str);

    void* objPtr = (void*)obj;
    bool isCoreModule = strcmp(fn->module->name->data, JSR_CORE_MODULE) == 0;

    if(isCoreModule) {
        jsrBufferAppendf(&str, "<%s %s@%p>", fnType, fn->name->data, objPtr);
    } else {
        jsrBufferAppendf(&str, "<%s %s.%s@%p>", fnType, fn->module->name->data, fn->name->data,
                         objPtr);
    }

    jsrBufferPush(&str);
    return true;
}
// end

// class Module
JSR_NATIVE(jsr_Module_string) {
    ObjModule* m = AS_MODULE(vm->apiStack[0]);
    JStarBuffer str;
    jsrBufferInit(vm, &str);
    jsrBufferAppendf(&str, "<module %s@%p>", m->name->data, (void*)m);
    jsrBufferPush(&str);
    return true;
}
// end

// class List
JSR_NATIVE(jsr_List_new) {
    JSR_CHECK(Int, 1, "size");

    double count = jsrGetNumber(vm, 1);
    if(count < 0) {
        JSR_RAISE(vm, "TypeException", "size must be >= 0");
    }

    ObjList* lst = newList(vm, count < 16 ? 16 : count);
    lst->count = count;
    push(vm, OBJ_VAL(lst));

    if(IS_CLOSURE(vm->apiStack[2]) || IS_NATIVE(vm->apiStack[2])) {
        for(size_t i = 0; i < lst->count; i++) {
            jsrPushValue(vm, 2);
            jsrPushNumber(vm, i);
            if(jsrCall(vm, 1) != JSR_SUCCESS) return false;
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
    ObjList* l = AS_LIST(vm->apiStack[0]);
    listAppend(vm, l, vm->apiStack[1]);
    jsrPushNull(vm);
    return true;
}

JSR_NATIVE(jsr_List_insert) {
    ObjList* l = AS_LIST(vm->apiStack[0]);
    size_t index = jsrCheckIndex(vm, 1, l->count + 1, "i");
    if(index == SIZE_MAX) return false;

    listInsert(vm, l, index, vm->apiStack[2]);
    jsrPushNull(vm);
    return true;
}

JSR_NATIVE(jsr_List_len) {
    push(vm, NUM_VAL(AS_LIST(vm->apiStack[0])->count));
    return true;
}

JSR_NATIVE(jsr_List_eq) {
    ObjList* lst = AS_LIST(vm->apiStack[0]);

    if(!IS_LIST(vm->apiStack[1])) {
        jsrPushBoolean(vm, false);
        return true;
    }

    ObjList* other = AS_LIST(vm->apiStack[1]);

    if(other->count != lst->count) {
        jsrPushBoolean(vm, false);
        return true;
    }

    bool res = false;
    if(!compareValues(vm, lst->arr, other->arr, lst->count, &res)) return false;

    jsrPushBoolean(vm, res);
    return true;
}

JSR_NATIVE(jsr_List_removeAt) {
    ObjList* l = AS_LIST(vm->apiStack[0]);
    size_t index = jsrCheckIndex(vm, 1, l->count, "i");
    if(index == SIZE_MAX) return false;

    Value r = l->arr[index];
    listRemove(vm, l, index);
    push(vm, r);
    return true;
}

JSR_NATIVE(jsr_List_clear) {
    AS_LIST(vm->apiStack[0])->count = 0;
    jsrPushNull(vm);
    return true;
}

typedef struct {
    JStarVM* vm;
    Value *list, *tmp;
    int64_t length;
    Value comparator;
} MergeState;

// Compare two values, calling the appropriate functions depending on the types
static bool lessEqCompare(JStarVM* vm, Value a, Value b, Value comparator, bool* out) {
    if(!IS_NULL(comparator)) {
        push(vm, comparator);
        push(vm, a);
        push(vm, b);

        if(jsrCall(vm, 2) != JSR_SUCCESS) return false;

        if(!IS_NUM(peek(vm))) {
            JSR_RAISE(vm, "TypeException", "`comparator` didn't return a Number, got %s",
                      getClass(vm, peek(vm))->name->data);
        }

        *out = AS_NUM(pop(vm)) <= 0;
    } else if(IS_NUM(a) && IS_NUM(b)) {
        *out = AS_NUM(a) <= AS_NUM(b);
    } else {
        push(vm, a);
        push(vm, b);

        if(jsrCallMethod(vm, "__le__", 1) != JSR_SUCCESS) return false;

        *out = valueToBool(pop(vm));
    }
    return true;
}

// Merge two ordered sublists [left:mid] [mid + 1 : right]
static bool merge(MergeState* state, int64_t left, int64_t mid, int64_t right) {
    Value* list = state->list;
    Value* tmp = state->tmp;
    int64_t length = state->length;
    Value comparator = state->comparator;

    int64_t k = left, i = left, j = mid + 1;
    while(i <= mid && j <= right) {
        bool isLessEq = false;
        if(!lessEqCompare(state->vm, list[i], list[j], comparator, &isLessEq)) {
            return false;
        }

        if(isLessEq) {
            tmp[k++] = list[i++];
        } else {
            tmp[k++] = list[j++];
        }
    }

    while(i < length && i <= mid) {
        tmp[k++] = list[i++];
    }

    for(int64_t i = left; i <= right; i++) {
        list[i] = tmp[i];
    }

    return true;
}

// Iterative bottom-up mergesort
static bool mergeSort(JStarVM* vm, Value* list, int64_t length, Value comp) {
    Value* tmp = malloc(sizeof(Value) * length);
    memcpy(tmp, list, sizeof(Value) * length);
    MergeState state = {vm, list, tmp, length, comp};

    int64_t high = length - 1;
    for(int64_t blk = 1; blk <= high; blk *= 2) {
        for(int64_t i = 0; i < high; i += 2 * blk) {
            int64_t left = i, mid = i + blk - 1, right = i + 2 * blk - 1;
            if(right > high) right = high;
            if(!merge(&state, left, mid, right)) {
                free(tmp);
                return false;
            }
        }
    }

    free(tmp);
    return true;
}

JSR_NATIVE(jsr_List_sort) {
    ObjList* list = AS_LIST(vm->apiStack[0]);
    Value comp = vm->apiStack[1];
    if(!mergeSort(vm, list->arr, list->count, comp)) return false;
    jsrPushNull(vm);
    return true;
}

JSR_NATIVE(jsr_List_iter) {
    ObjList* lst = AS_LIST(vm->apiStack[0]);

    if(IS_NULL(vm->apiStack[1]) && lst->count != 0) {
        push(vm, NUM_VAL(0));
        return true;
    }

    if(IS_NUM(vm->apiStack[1])) {
        size_t idx = (size_t)AS_NUM(vm->apiStack[1]);
        if(idx < lst->count - 1) {
            push(vm, NUM_VAL(idx + 1));
            return true;
        }
    }

    push(vm, BOOL_VAL(false));
    return true;
}

JSR_NATIVE(jsr_List_next) {
    ObjList* lst = AS_LIST(vm->apiStack[0]);

    if(IS_NUM(vm->apiStack[1])) {
        size_t idx = (size_t)AS_NUM(vm->apiStack[1]);
        if(idx < lst->count) {
            push(vm, lst->arr[idx]);
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
        JSR_FOREACH(
            1, {
                jsrListAppend(vm, 2);
                jsrPop(vm);
            }, )
    }

    ObjList* lst = AS_LIST(vm->sp[-1]);
    ObjTuple* tup = newTuple(vm, lst->count);
    if(lst->count > 0) memcpy(tup->arr, lst->arr, sizeof(Value) * lst->count);
    push(vm, OBJ_VAL(tup));
    return true;
}

JSR_NATIVE(jsr_Tuple_len) {
    push(vm, NUM_VAL(AS_TUPLE(vm->apiStack[0])->size));
    return true;
}

JSR_NATIVE(jsr_Tuple_eq) {
    ObjTuple* tup = AS_TUPLE(vm->apiStack[0]);

    if(!IS_TUPLE(vm->apiStack[1])) {
        jsrPushBoolean(vm, false);
        return true;
    }

    ObjTuple* other = AS_TUPLE(vm->apiStack[1]);

    if(other->size != tup->size) {
        jsrPushBoolean(vm, false);
        return true;
    }

    bool res = false;
    if(!compareValues(vm, tup->arr, other->arr, tup->size, &res)) return false;

    jsrPushBoolean(vm, res);
    return true;
}

JSR_NATIVE(jsr_Tuple_iter) {
    ObjTuple* tup = AS_TUPLE(vm->apiStack[0]);

    if(IS_NULL(vm->apiStack[1]) && tup->size != 0) {
        push(vm, NUM_VAL(0));
        return true;
    }

    if(IS_NUM(vm->apiStack[1])) {
        size_t idx = (size_t)AS_NUM(vm->apiStack[1]);
        if(idx < tup->size - 1) {
            push(vm, NUM_VAL(idx + 1));
            return true;
        }
    }

    push(vm, BOOL_VAL(false));
    return true;
}

JSR_NATIVE(jsr_Tuple_next) {
    ObjTuple* tup = AS_TUPLE(vm->apiStack[0]);

    if(IS_NUM(vm->apiStack[1])) {
        size_t idx = (size_t)AS_NUM(vm->apiStack[1]);
        if(idx < tup->size) {
            push(vm, tup->arr[idx]);
            return true;
        }
    }

    push(vm, NULL_VAL);
    return true;
}

JSR_NATIVE(jsr_Tuple_hash) {
    ObjTuple* tup = AS_TUPLE(vm->apiStack[0]);

    uint32_t hash = 1;
    for(size_t i = 0; i < tup->size; i++) {
        push(vm, tup->arr[i]);
        if(jsrCallMethod(vm, "__hash__", 0) != JSR_SUCCESS) return false;
        JSR_CHECK(Number, -1, "__hash__() return value");
        uint32_t elemHash = jsrGetNumber(vm, -1);
        pop(vm);

        hash = 31 * hash + elemHash;
    }

    jsrPushNumber(vm, hash);
    return true;
}
// end

// class String
JSR_NATIVE(jsr_String_new) {
    JStarBuffer string;
    jsrBufferInit(vm, &string);

    JSR_FOREACH(
        1,
        {
            if(jsrCallMethod(vm, "__string__", 0) != JSR_SUCCESS) {
                jsrBufferFree(&string);
                return false;
            }
            if(!jsrIsString(vm, -1)) {
                jsrBufferFree(&string);
                JSR_RAISE(vm, "TypeException", "__string__() didn't return a String");
            }
            jsrBufferAppendStr(&string, jsrGetString(vm, -1));
            jsrPop(vm);
        },
        jsrBufferFree(&string));

    jsrBufferPush(&string);
    return true;
}

JSR_NATIVE(jsr_String_charAt) {
    JSR_CHECK(Int, 1, "idx");

    ObjString* str = AS_STRING(vm->apiStack[0]);
    size_t i = jsrCheckIndex(vm, 1, str->length, "idx");
    if(i == SIZE_MAX) return false;

    int c = str->data[i];
    jsrPushNumber(vm, (double)c);
    return true;
}

JSR_NATIVE(jsr_String_startsWith) {
    JSR_CHECK(String, 1, "prefix");
    JSR_CHECK(Int, 2, "offset");

    const char* prefix = jsrGetString(vm, 1);
    size_t prefixLen = jsrGetStringSz(vm, 1);
    int offset = jsrGetNumber(vm, 2);
    size_t thisLen = jsrGetStringSz(vm, 0);

    if(offset < 0 || thisLen < (size_t)offset || thisLen - offset < prefixLen) {
        jsrPushBoolean(vm, false);
        return true;
    }

    const char* thisStr = jsrGetString(vm, 0) + offset;
    if(memcmp(thisStr, prefix, prefixLen) == 0) {
        jsrPushBoolean(vm, true);
        return true;
    }

    jsrPushBoolean(vm, false);
    return true;
}

JSR_NATIVE(jsr_String_endsWith) {
    JSR_CHECK(String, 1, "suffix");

    const char* suffix = jsrGetString(vm, 1);
    size_t suffixLen = jsrGetStringSz(vm, 1);
    size_t thisLen = jsrGetStringSz(vm, 0);

    if(thisLen < suffixLen) {
        jsrPushBoolean(vm, false);
        return true;
    }

    const char* thisStr = jsrGetString(vm, 0) + (thisLen - suffixLen);

    if(memcmp(thisStr, suffix, suffixLen) == 0) {
        jsrPushBoolean(vm, true);
        return true;
    }

    jsrPushBoolean(vm, false);
    return true;
}

JSR_NATIVE(jsr_String_strip) {
    const char* str = jsrGetString(vm, 0);
    size_t start = 0, end = jsrGetStringSz(vm, 0);

    while(start < end && isspace(str[start])) {
        start++;
    }

    while(start < end && isspace(str[end - 1])) {
        end--;
    }

    if(start == end) {
        jsrPushString(vm, "");
    } else if(start != 0 || end != jsrGetStringSz(vm, 0)) {
        jsrPushStringSz(vm, &str[start], (size_t)(end - start));
    } else {
        jsrPushValue(vm, 0);
    }

    return true;
}

JSR_NATIVE(jsr_String_chomp) {
    const char* str = jsrGetString(vm, 0);
    size_t end = jsrGetStringSz(vm, 0);

    while(end > 0 && isspace(str[end - 1])) {
        end--;
    }

    if(end != jsrGetStringSz(vm, 0)) {
        jsrPushStringSz(vm, str, end);
    } else {
        jsrPushValue(vm, 0);
    }

    return true;
}

JSR_NATIVE(jsr_String_join) {
    JStarBuffer joined;
    jsrBufferInit(vm, &joined);

    JSR_FOREACH(
        1,
        {
            if(!jsrIsString(vm, -1)) {
                if((jsrCallMethod(vm, "__string__", 0) != JSR_SUCCESS)) {
                    jsrBufferFree(&joined);
                    return false;
                }
                if(!jsrIsString(vm, -1)) {
                    jsrBufferFree(&joined);
                    JSR_RAISE(vm, "TypeException", "s.__string__() didn't return a String");
                }
            }
            jsrBufferAppend(&joined, jsrGetString(vm, -1), jsrGetStringSz(vm, -1));
            jsrBufferAppend(&joined, jsrGetString(vm, 0), jsrGetStringSz(vm, 0));
            jsrPop(vm);
        },
        jsrBufferFree(&joined))

    if(joined.size > 0) {
        jsrBufferTrunc(&joined, joined.size - jsrGetStringSz(vm, 0));
    }

    jsrBufferPush(&joined);
    return true;
}

static bool getFmtArgument(JStarVM* vm, Value args, size_t i, Value* out) {
    if(IS_TUPLE(args)) {
        ObjTuple* argsTuple = AS_TUPLE(args);
        size_t idx = jsrCheckIndexNum(vm, i, argsTuple->size);
        if(idx == SIZE_MAX) return false;
        *out = argsTuple->arr[i];
        return true;
    } else {
        size_t idx = jsrCheckIndexNum(vm, i, 1);
        if(idx == SIZE_MAX) return false;
        *out = args;
        return true;
    }
}

JSR_NATIVE(jsr_String_mod) {
    Value fmtArgs = vm->apiStack[1];
    const char* format = jsrGetString(vm, 0);
    const char* formatEnd = format + jsrGetStringSz(vm, 0);

    JStarBuffer buf;
    jsrBufferInit(vm, &buf);

    for(const char* ptr = format; ptr < formatEnd; ptr++) {
        if(*ptr == '{' && isdigit(ptr[1])) {
            char* end;
            int n = strtol(ptr + 1, &end, 10);
            if(end != ptr + 1 && *end == '}') {
                Value fmtArg;
                if(!getFmtArgument(vm, fmtArgs, n, &fmtArg)) {
                    jsrBufferFree(&buf);
                    return false;
                }
                push(vm, fmtArg);

                if(jsrCallMethod(vm, "__string__", 0) != JSR_SUCCESS) {
                    jsrBufferFree(&buf);
                    return false;
                }

                if(!jsrIsString(vm, -1)) {
                    jsrBufferFree(&buf);
                    JSR_RAISE(vm, "TypeException", "%s.__string__() didn't return a String.",
                              getClass(vm, fmtArg)->name->data);
                }

                jsrBufferAppendStr(&buf, jsrGetString(vm, -1));
                jsrPop(vm);

                ptr = end;  // skip the format specifier
                continue;
            }
        }
        jsrBufferAppend(&buf, ptr, 1);
    }

    jsrBufferPush(&buf);
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

    ObjString* s1 = AS_STRING(vm->apiStack[0]);
    ObjString* s2 = AS_STRING(vm->apiStack[1]);

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
    ObjString* s = AS_STRING(vm->apiStack[0]);

    if(IS_NULL(vm->apiStack[1]) && s->length != 0) {
        push(vm, NUM_VAL(0));
        return true;
    }

    if(IS_NUM(vm->apiStack[1])) {
        size_t idx = (size_t)AS_NUM(vm->apiStack[1]);
        if(idx < s->length - 1) {
            push(vm, NUM_VAL(idx + 1));
            return true;
        }
    }

    push(vm, BOOL_VAL(false));
    return true;
}

JSR_NATIVE(jsr_String_next) {
    ObjString* str = AS_STRING(vm->apiStack[0]);

    if(IS_NUM(vm->apiStack[1])) {
        size_t idx = (size_t)AS_NUM(vm->apiStack[1]);
        if(idx < str->length) {
            jsrPushStringSz(vm, str->data + idx, 1);
            return true;
        }
    }

    push(vm, NULL_VAL);
    return true;
}
// end

// class Table
#define MAX_LOAD_FACTOR  0.75
#define INITIAL_CAPACITY 8
#define GROW_FACTOR      2

static bool tableKeyHash(JStarVM* vm, Value key, uint32_t* hash) {
    if(IS_STRING(key)) {
        *hash = STRING_GET_HASH(AS_STRING(key));
        return true;
    }
    if(IS_NUM(key)) {
        *hash = hashNumber(AS_NUM(key));
        return true;
    }
    if(IS_BOOL(key)) {
        *hash = AS_BOOL(key);
        return true;
    }

    push(vm, key);
    if(jsrCallMethod(vm, "__hash__", 0) != JSR_SUCCESS) return false;
    JSR_CHECK(Number, -1, "__hash__() return value");
    *hash = (uint32_t)jsrGetNumber(vm, -1);
    pop(vm);
    return hash;
}

static bool tableKeyEquals(JStarVM* vm, Value k1, Value k2, bool* eq) {
    if(IS_STRING(k1) || IS_NUM(k1) || IS_BOOL(k1)) {
        *eq = valueEquals(k1, k2);
        return true;
    }

    push(vm, k1);
    push(vm, k2);
    if(jsrCallMethod(vm, "__eq__", 1) != JSR_SUCCESS) return false;
    *eq = valueToBool(pop(vm));
    return true;
}

static bool findEntry(JStarVM* vm, TableEntry* entries, size_t sizeMask, Value key,
                      TableEntry** out) {
    uint32_t hash;
    if(!tableKeyHash(vm, key, &hash)) return false;

    size_t i = hash & sizeMask;
    TableEntry* tomb = NULL;

    for(;;) {
        TableEntry* e = &entries[i];
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
            bool eq;
            if(!tableKeyEquals(vm, key, e->key, &eq)) return false;
            if(eq) {
                *out = e;
                return true;
            }
        }
        i = (i + 1) & sizeMask;
    }
}

static void growEntries(JStarVM* vm, ObjTable* t) {
    size_t newSize = t->sizeMask ? (t->sizeMask + 1) * GROW_FACTOR : INITIAL_CAPACITY;
    TableEntry* newEntries = GC_ALLOC(vm, sizeof(TableEntry) * newSize);
    for(size_t i = 0; i < newSize; i++) {
        newEntries[i].key = NULL_VAL;
        newEntries[i].val = NULL_VAL;
    }

    t->numEntries = 0, t->count = 0;
    if(t->sizeMask != 0) {
        for(size_t i = 0; i <= t->sizeMask; i++) {
            TableEntry* e = &t->entries[i];
            if(IS_NULL(e->key)) continue;

            TableEntry* dest;
            findEntry(vm, newEntries, newSize - 1, e->key, &dest);
            dest->key = e->key;
            dest->val = e->val;
            t->numEntries++, t->count++;
        }
        GC_FREE_ARRAY(vm, TableEntry, t->entries, t->sizeMask + 1);
    }
    t->entries = newEntries;
    t->sizeMask = newSize - 1;
}

JSR_NATIVE(jsr_Table_get) {
    if(jsrIsNull(vm, 1)) JSR_RAISE(vm, "TypeException", "Key of Table cannot be null.");

    ObjTable* t = AS_TABLE(vm->apiStack[0]);
    if(t->entries == NULL) {
        push(vm, NULL_VAL);
        return true;
    }

    TableEntry* e;
    if(!findEntry(vm, t->entries, t->sizeMask, vm->apiStack[1], &e)) {
        return false;
    }

    if(!IS_NULL(e->key)) {
        push(vm, e->val);
    } else {
        push(vm, NULL_VAL);
    }

    return true;
}

JSR_NATIVE(jsr_Table_set) {
    if(jsrIsNull(vm, 1)) JSR_RAISE(vm, "TypeException", "Key of Table cannot be null.");

    ObjTable* t = AS_TABLE(vm->apiStack[0]);
    if(t->numEntries + 1 > (t->sizeMask + 1) * MAX_LOAD_FACTOR) {
        growEntries(vm, t);
    }

    TableEntry* e;
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
    ObjTable* t = AS_TABLE(vm->apiStack[0]);

    if(t->entries == NULL) {
        push(vm, BOOL_VAL(false));
        return true;
    }

    TableEntry* toDelete;
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
    ObjTable* t = AS_TABLE(vm->apiStack[0]);
    t->numEntries = t->count = 0;
    for(size_t i = 0; i < t->sizeMask + 1; i++) {
        t->entries[i].key = NULL_VAL;
        t->entries[i].val = NULL_VAL;
    }
    push(vm, NULL_VAL);
    return true;
}

JSR_NATIVE(jsr_Table_len) {
    ObjTable* t = AS_TABLE(vm->apiStack[0]);
    push(vm, NUM_VAL(t->count));
    return true;
}

JSR_NATIVE(jsr_Table_contains) {
    if(jsrIsNull(vm, 0)) JSR_RAISE(vm, "TypeException", "Key of Table cannot be null.");

    ObjTable* t = AS_TABLE(vm->apiStack[0]);
    if(t->entries == NULL) {
        push(vm, BOOL_VAL(false));
        return true;
    }

    TableEntry* e;
    if(!findEntry(vm, t->entries, t->sizeMask, vm->apiStack[1], &e)) {
        return false;
    }

    push(vm, BOOL_VAL(!IS_NULL(e->key)));
    return true;
}

JSR_NATIVE(jsr_Table_keys) {
    ObjTable* t = AS_TABLE(vm->apiStack[0]);
    TableEntry* entries = t->entries;

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
    ObjTable* t = AS_TABLE(vm->apiStack[0]);
    TableEntry* entries = t->entries;

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
    ObjTable* t = AS_TABLE(vm->apiStack[0]);

    if(IS_NULL(vm->apiStack[1]) && t->entries == NULL) {
        push(vm, BOOL_VAL(false));
        return true;
    }

    size_t lastIdx = 0;

    if(IS_NUM(vm->apiStack[1])) {
        size_t idx = (size_t)AS_NUM(vm->apiStack[1]);
        if(idx >= t->sizeMask) {
            push(vm, BOOL_VAL(false));
            return true;
        }
        lastIdx = idx + 1;
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
    ObjTable* t = AS_TABLE(vm->apiStack[0]);

    if(IS_NUM(vm->apiStack[1])) {
        size_t idx = (size_t)AS_NUM(vm->apiStack[1]);
        if(idx <= t->sizeMask) {
            push(vm, t->entries[idx].key);
            return true;
        }
    }

    push(vm, NULL_VAL);
    return true;
}

JSR_NATIVE(jsr_Table_string) {
    ObjTable* t = AS_TABLE(vm->apiStack[0]);

    JStarBuffer buf;
    jsrBufferInit(vm, &buf);
    jsrBufferAppendChar(&buf, '{');

    TableEntry* entries = t->entries;
    if(entries != NULL) {
        for(size_t i = 0; i < t->sizeMask + 1; i++) {
            if(!IS_NULL(entries[i].key)) {
                push(vm, entries[i].key);
                if(jsrCallMethod(vm, "__string__", 0) != JSR_SUCCESS || !jsrIsString(vm, -1)) {
                    jsrBufferFree(&buf);
                    return false;
                }
                jsrBufferAppendStr(&buf, jsrGetString(vm, -1));
                jsrBufferAppendStr(&buf, " : ");
                jsrPop(vm);

                push(vm, entries[i].val);
                if(jsrCallMethod(vm, "__string__", 0) != JSR_SUCCESS || !jsrIsString(vm, -1)) {
                    jsrBufferFree(&buf);
                    return false;
                }
                jsrBufferAppendStr(&buf, jsrGetString(vm, -1));
                jsrBufferAppendStr(&buf, ", ");
                jsrPop(vm);
            }
        }
        jsrBufferTrunc(&buf, buf.size - 2);
    }
    jsrBufferAppendChar(&buf, '}');
    jsrBufferPush(&buf);
    return true;
}
// end

// class Enum
#define M_VALUE_NAME "_valueName"

static bool checkEnumElem(JStarVM* vm, int slot) {
    if(!jsrIsString(vm, slot)) {
        JSR_RAISE(vm, "TypeException", "Enum element must be a String");
    }

    ObjInstance* inst = AS_INSTANCE(vm->apiStack[0]);
    const char* enumElem = jsrGetString(vm, slot);
    size_t enumElemLen = jsrGetStringSz(vm, slot);

    if(isalpha(enumElem[0])) {
        for(size_t i = 1; i < enumElemLen; i++) {
            char c = enumElem[i];
            if(!isalpha(c) && !isdigit(c) && c != '_') {
                JSR_RAISE(vm, "InvalidArgException", "Invalid Enum element `%s`", enumElem);
            }
        }

        ObjString* str = AS_STRING(apiStackSlot(vm, slot));
        if(hashTableContainsKey(&inst->fields, str)) {
            JSR_RAISE(vm, "InvalidArgException", "Duplicate Enum element `%s`", enumElem);
        }

        return true;
    }

    JSR_RAISE(vm, "InvalidArgException", "Invalid Enum element `%s`", enumElem);
}

JSR_NATIVE(jsr_Enum_new) {
    jsrPushTable(vm);
    jsrSetField(vm, 0, M_VALUE_NAME);
    jsrPop(vm);

    if(jsrTupleGetLength(vm, 1) == 0) {
        JSR_RAISE(vm, "InvalidArgException", "Cannot create empty Enum");
    }

    jsrTupleGet(vm, 0, 1);
    bool customEnum = jsrIsTable(vm, -1);
    if(!customEnum) {
        jsrPop(vm);
        jsrPushValue(vm, 1);
    }

    int i = 0;
    JSR_FOREACH(
        2, {
            if(!checkEnumElem(vm, -1)) return false;

            if(customEnum) {
                jsrPushValue(vm, 2);
                jsrPushValue(vm, -2);
                if(jsrCallMethod(vm, "__get__", 1) != JSR_SUCCESS) return false;
            } else {
                jsrPushNumber(vm, i);
            }

            jsrSetField(vm, 0, jsrGetString(vm, -2));
            jsrPop(vm);

            if(!jsrGetField(vm, 0, M_VALUE_NAME)) return false;

            if(customEnum) {
                jsrPushValue(vm, 2);
                jsrPushValue(vm, -3);
                if(jsrCallMethod(vm, "__get__", 1) != JSR_SUCCESS) return false;
            } else {
                jsrPushNumber(vm, i);
            }

            jsrPushValue(vm, -3);
            if(jsrCallMethod(vm, "__set__", 2) != JSR_SUCCESS) return false;
            jsrPop(vm);

            jsrPop(vm);
            i++;
        }, );

    if(i == 0) {
        JSR_RAISE(vm, "InvalidArgException", "Cannot create empty Enum");
    }

    jsrPop(vm);
    jsrPushValue(vm, 0);
    return true;
}

JSR_NATIVE(jsr_Enum_value) {
    if(!jsrGetString(vm, 1)) return false;
    if(!jsrGetField(vm, 0, jsrGetString(vm, 1))) jsrPushNull(vm);
    return true;
}

JSR_NATIVE(jsr_Enum_name) {
    if(!jsrGetField(vm, 0, M_VALUE_NAME)) return false;
    jsrPushValue(vm, 1);
    if(jsrCallMethod(vm, "__get__", 1) != JSR_SUCCESS) return false;
    return true;
}
// end

// -----------------------------------------------------------------------------
// BUILTIN FUNCTIONS
// -----------------------------------------------------------------------------

JSR_NATIVE(jsr_int) {
    if(jsrIsNumber(vm, 1)) {
        jsrPushNumber(vm, trunc(jsrGetNumber(vm, 1)));
        return true;
    }
    if(jsrIsString(vm, 1)) {
        char* end = NULL;
        const char* nstr = jsrGetString(vm, 1);
        long long n = strtoll(nstr, &end, 10);

        if((n == 0 && end == nstr) || *end != '\0') {
            JSR_RAISE(vm, "InvalidArgException", "'%s'.", nstr);
        }
        if(n == LLONG_MAX) {
            JSR_RAISE(vm, "InvalidArgException", "Overflow: '%s'.", nstr);
        }
        if(n == LLONG_MIN) {
            JSR_RAISE(vm, "InvalidArgException", "Underflow: '%s'.", nstr);
        }

        jsrPushNumber(vm, n);
        return true;
    }
    JSR_RAISE(vm, "TypeException", "Argument must be a number or a string.");
}

JSR_NATIVE(jsr_char) {
    JSR_CHECK(String, 1, "c");
    const char* str = jsrGetString(vm, 1);
    if(jsrGetStringSz(vm, 1) != 1) {
        JSR_RAISE(vm, "InvalidArgException", "c must be a String of length 1");
    }
    int c = str[0];
    jsrPushNumber(vm, (double)c);
    return true;
}

JSR_NATIVE(jsr_garbageCollect) {
    garbageCollect(vm);
    jsrPushNull(vm);
    return true;
}

JSR_NATIVE(jsr_importPaths) {
    push(vm, OBJ_VAL(vm->importPaths));
    return true;
}

JSR_NATIVE(jsr_ascii) {
    JSR_CHECK(Int, 1, "num");
    char c = jsrGetNumber(vm, 1);
    jsrPushStringSz(vm, &c, 1);
    return true;
}

JSR_NATIVE(jsr_print) {
    jsrPushValue(vm, 1);
    if(jsrCallMethod(vm, "__string__", 0) != JSR_SUCCESS) return false;
    if(!jsrIsString(vm, -1)) {
        JSR_RAISE(vm, "TypeException", "s.__string__() didn't return a String");
    }

    printf("%s", jsrGetString(vm, -1));
    jsrPop(vm);

    JSR_FOREACH(
        2, {
            if(jsrCallMethod(vm, "__string__", 0) != JSR_SUCCESS) return false;
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
    JSR_CHECK(String, 1, "source");

    if(vm->frameCount < 1) {
        JSR_RAISE(vm, "Exception", "eval() can only be called by another function");
    }

    Obj* prevFn = vm->frames[vm->frameCount - 2].fn;

    ObjModule* mod = NULL;
    if(prevFn->type == OBJ_CLOSURE) {
        mod = ((ObjClosure*)prevFn)->fn->c.module;
    } else {
        mod = ((ObjNative*)prevFn)->c.module;
    }

    JStarStmt* program = jsrParse("<eval>", jsrGetString(vm, 1), parseErrorCallback, vm);
    if(program == NULL) {
        JSR_RAISE(vm, "SyntaxException", "Syntax error");
    }

    ObjFunction* fn = compileWithModule(vm, "<eval>", mod->name, program);
    jsrStmtFree(program);

    if(fn == NULL) {
        JSR_RAISE(vm, "SyntaxException", "Syntax error");
    }

    push(vm, OBJ_VAL(fn));
    ObjClosure* closure = newClosure(vm, fn);
    pop(vm);

    push(vm, OBJ_VAL(closure));
    if(jsrCall(vm, 0) != JSR_SUCCESS) return false;
    pop(vm);

    jsrPushNull(vm);
    return true;
}

JSR_NATIVE(jsr_type) {
    push(vm, OBJ_VAL(getClass(vm, peek(vm))));
    return true;
}

// -----------------------------------------------------------------------------
// BUILTIN EXCEPTIONS
// -----------------------------------------------------------------------------

// class Exception
#define INDENT "    "

static bool recordEquals(FrameRecord* f1, FrameRecord* f2) {
    if(f1 && f2) {
        return (strcmp(f1->moduleName->data, f2->moduleName->data) == 0) &&
               (strcmp(f1->funcName->data, f2->funcName->data) == 0) && (f1->line == f2->line);
    }
    return false;
}

JSR_NATIVE(jsr_Exception_printStacktrace) {
    Value stval = NULL_VAL;
    ObjInstance* exc = AS_INSTANCE(vm->apiStack[0]);
    hashTableGet(&exc->fields, copyString(vm, EXC_TRACE, strlen(EXC_TRACE)), &stval);

    if(!IS_STACK_TRACE(stval)) {
        jsrPushNull(vm);
        return true;
    }

    Value cause = NULL_VAL;
    hashTableGet(&exc->fields, copyString(vm, EXC_CAUSE, strlen(EXC_CAUSE)), &cause);
    if(isInstance(vm, cause, vm->excClass)) {
        push(vm, cause);
        jsrCallMethod(vm, "printStacktrace", 0);
        pop(vm);
        fprintf(stderr, "\nAbove Excetption caused:\n");
    }

    ObjStackTrace* st = AS_STACK_TRACE(stval);

    if(st->recordCount > 0) {
        FrameRecord* lastRecord = NULL;

        fprintf(stderr, "Traceback (most recent call last):\n");
        for(int i = st->recordCount - 1; i >= 0; i--) {
            FrameRecord* record = &st->records[i];

            if(recordEquals(lastRecord, record)) {
                int repetitions = 1;
                while(i > 0) {
                    record = &st->records[i - 1];
                    if(!recordEquals(lastRecord, record)) break;
                    repetitions++, i--;
                }
                fprintf(stderr, INDENT "...\n");
                fprintf(stderr, INDENT "[Previous line repeated %d times]\n", repetitions);
                continue;
            }

            fprintf(stderr, INDENT);

            if(record->line >= 0) {
                fprintf(stderr, "[line %d]", record->line);
            } else {
                fprintf(stderr, "[line ?]");
            }
            fprintf(stderr, " module %s in %s\n", record->moduleName->data, record->funcName->data);

            lastRecord = record;
        }
    }

    Value err = NULL_VAL;
    hashTableGet(&exc->fields, copyString(vm, EXC_ERR, strlen(EXC_ERR)), &err);

    if(IS_STRING(err) && AS_STRING(err)->length > 0) {
        fprintf(stderr, "%s: %s\n", exc->base.cls->name->data, AS_STRING(err)->data);
    } else {
        fprintf(stderr, "%s\n", exc->base.cls->name->data);
    }

    jsrPushNull(vm);
    return true;
}

JSR_NATIVE(jsr_Exception_getStacktrace) {
    Value stval = NULL_VAL;
    ObjInstance* exc = AS_INSTANCE(vm->apiStack[0]);
    hashTableGet(&exc->fields, copyString(vm, EXC_TRACE, strlen(EXC_TRACE)), &stval);

    if(!IS_STACK_TRACE(stval)) {
        jsrPushString(vm, "");
        return true;
    }

    JStarBuffer string;
    jsrBufferInitCapacity(vm, &string, 64);

    Value cause = NULL_VAL;
    hashTableGet(&exc->fields, copyString(vm, EXC_CAUSE, strlen(EXC_CAUSE)), &cause);
    if(isInstance(vm, cause, vm->excClass)) {
        push(vm, cause);
        jsrCallMethod(vm, "getStacktrace", 0);
        Value stackTrace = peek(vm);
        if(IS_STRING(stackTrace)) {
            jsrBufferAppend(&string, AS_STRING(stackTrace)->data, AS_STRING(stackTrace)->length);
            jsrBufferAppendStr(&string, "\n\nAbove Exception caused:\n");
        }
        pop(vm);
    }

    ObjStackTrace* st = AS_STACK_TRACE(stval);

    if(st->recordCount > 0) {
        FrameRecord* lastRecord = NULL;

        jsrBufferAppendf(&string, "Traceback (most recent call last):\n");
        for(int i = st->recordCount - 1; i >= 0; i--) {
            FrameRecord* record = &st->records[i];

            if(recordEquals(lastRecord, record)) {
                int repetitions = 1;
                while(i > 0) {
                    record = &st->records[i - 1];
                    if(!recordEquals(lastRecord, record)) break;
                    repetitions++, i--;
                }
                jsrBufferAppendStr(&string, INDENT "...\n");
                jsrBufferAppendf(&string, INDENT "[Previous line repeated %d times]\n",
                                 repetitions);
                continue;
            }

            jsrBufferAppendStr(&string, "    ");
            
            if(record->line >= 0) {
                jsrBufferAppendf(&string, "[line %d]", record->line);
            } else {
                jsrBufferAppendStr(&string, "[line ?]");
            }

            jsrBufferAppendf(&string, " module %s in %s\n", record->moduleName->data,
                             record->funcName->data);

            lastRecord = record;
        }
    }

    Value err = NULL_VAL;
    hashTableGet(&exc->fields, copyString(vm, EXC_ERR, strlen(EXC_ERR)), &err);

    if(IS_STRING(err) && AS_STRING(err)->length > 0)
        jsrBufferAppendf(&string, "%s: %s", exc->base.cls->name->data, AS_STRING(err)->data);
    else
        jsrBufferAppendf(&string, "%s", exc->base.cls->name->data);

    jsrBufferPush(&string);
    return true;
}
// end
