#include "core.h"

#include <ctype.h>
#include <errno.h>
#include <float.h>
#include <inttypes.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../builtins.h"
#include "gc.h"
#include "import.h"
#include "int_hashtable.h"
#include "jstar.h"
#include "object.h"
#include "object_types.h"
#include "profiler.h"
#include "util.h"
#include "value.h"
#include "value_hashtable.h"
#include "vm.h"

#define INT_PRINT_CUTOFF (INT64_C(1) << DBL_MANT_DIG)

// The top-level variables defined by the core module
// TODO: auto-generate from core/*jsr files
static const char* coreSymbols[] = {
    // Module variables
    MOD_NAME,
    MOD_PATH,
    MOD_THIS,

    // import __core__.excs
    "excs",
    "Exception",
    "TypeException",
    "NameException",
    "FieldException",
    "MethodException",
    "ImportException",
    "StackOverflowException",
    "SyntaxException",
    "InvalidArgException",
    "GeneratorException",
    "IndexOutOfBoundException",
    "AssertException",
    "NotImplementedException",
    "ProgramInterrupt",

    // import __core__.std
    "std",
    "assert",
    "print",
    "type",
    "typeAssert",

    // import __core__.iter
    "iter",

    // __core__
    "argv",
    "importPaths",
    "Number",
    "Boolean",
    "Null",
    "Function",
    "Module",
    "Generator",
    "String",
    "List",
    "Tuple",
    "Table",
    "Enum",
    "StackTrace",
    "Userdata",

    NULL,
};

static ObjClass* createClass(JStarVM* vm, ObjModule* m, ObjClass* sup, const char* name) {
    ObjString* n = copyString(vm, name, strlen(name));
    push(vm, OBJ_VAL(n));
    ObjClass* c = newClass(vm, n, sup);
    pop(vm);
    moduleSetGlobal(vm, m, n, OBJ_VAL(c));
    return c;
}

static Value getDefinedName(JStarVM* vm, ObjModule* m, const char* name) {
    Value v = NULL_VAL;
    moduleGetGlobal(vm, m, copyString(vm, name, strlen(name)), &v);
    return v;
}

static void defMethod(JStarVM* vm, ObjModule* m, ObjClass* cls, JStarNative nat, const char* name,
                      uint8_t argc) {
    ObjString* nativeName = copyString(vm, name, strlen(name));
    ObjNative* native = newNative(vm, m, nativeName, argc, 0, false, nat);
    hashTableValuePut(&cls->methods, nativeName, OBJ_VAL(native));
}

static uint64_t hash64(uint64_t x) {
    x = (x ^ (x >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
    x = (x ^ (x >> 27)) * UINT64_C(0x94d049bb133111eb);
    x = x ^ (x >> 31);
    return x;
}

static uint32_t hashNumber(double num) {
    return (uint32_t)hash64(REINTERPRET_CAST(double, uint64_t, num == -0 ? 0 : num));
}

static bool compareValues(JStarVM* vm, const Value* v1, const Value* v2, size_t size, bool* out) {
    *out = true;
    for(size_t i = 0; i < size; i++) {
        push(vm, v1[i]);
        push(vm, v2[i]);

        if(jsrCallMethod(vm, "__eq__", 1) != JSR_SUCCESS) {
            return false;
        }

        bool res = valueToBool(pop(vm));
        if(!res) {
            *out = false;
            return true;
        }
    }
    return true;
}

// -----------------------------------------------------------------------------
// API
// -----------------------------------------------------------------------------

static JSR_NATIVE(jsr_Object_string);
static JSR_NATIVE(jsr_Object_hash);
static JSR_NATIVE(jsr_Object_eq);
static JSR_NATIVE(jsr_Class_getName);
static JSR_NATIVE(jsr_Class_implements);
static JSR_NATIVE(jsr_Class_string);

void initCoreModule(JStarVM* vm) {
    PROFILE_FUNC()

    // Create and register core module
    ObjString* coreModName = copyString(vm, JSR_CORE_MODULE, strlen(JSR_CORE_MODULE));

    push(vm, OBJ_VAL(coreModName));
    ObjModule* core = newModule(vm, JSR_CORE_MODULE, coreModName);
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
    hashTableValueMerge(&vm->clsClass->methods, &vm->objClass->methods);
    defMethod(vm, core, vm->clsClass, &jsr_Class_getName, "getName", 0);
    defMethod(vm, core, vm->clsClass, &jsr_Class_implements, "implements", 1);
    defMethod(vm, core, vm->clsClass, &jsr_Class_string, "__string__", 0);

    {
        PROFILE("{core-runEval}::initCore")

        // Read core module
        size_t len;
        const char* code = readBuiltInModule(JSR_CORE_MODULE, &len);

        // Execute core module
        JStarResult res = jsrEvalModule(vm, JSR_CORE_MODULE, JSR_CORE_MODULE, code, len);

        JSR_ASSERT(res == JSR_SUCCESS, "Core module bootsrap failed");
        (void)res;  // Not actually used aside from the assert
    }

    // Cache builtin class objects in JStarVM
    {
        PROFILE("{cache-bltins}::initCore")

        vm->strClass = AS_CLASS(getDefinedName(vm, core, "String"));
        vm->boolClass = AS_CLASS(getDefinedName(vm, core, "Boolean"));
        vm->lstClass = AS_CLASS(getDefinedName(vm, core, "List"));
        vm->numClass = AS_CLASS(getDefinedName(vm, core, "Number"));
        vm->funClass = AS_CLASS(getDefinedName(vm, core, "Function"));
        vm->genClass = AS_CLASS(getDefinedName(vm, core, "Generator"));
        vm->modClass = AS_CLASS(getDefinedName(vm, core, "Module"));
        vm->nullClass = AS_CLASS(getDefinedName(vm, core, "Null"));
        vm->stClass = AS_CLASS(getDefinedName(vm, core, "StackTrace"));
        vm->tupClass = AS_CLASS(getDefinedName(vm, core, "Tuple"));
        vm->excClass = AS_CLASS(getDefinedName(vm, core, "Exception"));
        vm->tableClass = AS_CLASS(getDefinedName(vm, core, "Table"));
        vm->udataClass = AS_CLASS(getDefinedName(vm, core, "Userdata"));
        core->base.cls = vm->modClass;

        // Cache core module global objects in vm
        vm->argv = AS_LIST(getDefinedName(vm, core, "argv"));
    }

    {
        PROFILE("{patch-up-classes}::initCoreModule")

        // Patch up the class field of any object that was allocated
        // before the creation of its corresponding class object
        for(Obj* o = vm->objects; o != NULL; o = o->next) {
            if(o->type == OBJ_UPVALUE) continue;

            if(o->type == OBJ_STRING) {
                o->cls = vm->strClass;
            } else if(o->type == OBJ_LIST) {
                o->cls = vm->lstClass;
            } else if(o->type == OBJ_MODULE) {
                o->cls = vm->modClass;
            } else if(o->type == OBJ_CLOSURE || o->type == OBJ_FUNCTION || o->type == OBJ_NATIVE) {
                o->cls = vm->funClass;
            }

            // Ensure all allocated object do actually have a class reference!
            JSR_ASSERT(o->cls, "Object without class reference");
        }
    }
}

bool resolveCoreSymbol(const JStarIdentifier* id) {
    for(const char** name = coreSymbols; *name; name++) {
        size_t len = strlen(*name);
        if(len == id->length && memcmp(id->name, *name, len) == 0) {
            return true;
        }
    }
    return false;
}

// -----------------------------------------------------------------------------
// BUILTIN CLASSES
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

static JSR_NATIVE(jsr_Class_implements) {
    JSR_CHECK(String, 1, "method");
    ObjClass* cls = AS_CLASS(vm->apiStack[0]);
    ObjString* method = AS_STRING(vm->apiStack[1]);
    push(vm, BOOL_VAL(hashTableValueContainsKey(&cls->methods, method)));
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

// class Number
JSR_NATIVE(jsr_Number_construct) {
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
    double num = AS_NUM(vm->apiStack[0]);
    if(trunc(num) == num && num > -INT_PRINT_CUTOFF && num < INT_PRINT_CUTOFF) {
        char string[STRLEN_FOR_INT(int64_t)];
        int written = sprintf(string, "%" PRId64, (int64_t)num);
        jsrPushStringSz(vm, string, written);
    } else {
        char string[24];  // enough for .*g with DBL_DIG
        int written = sprintf(string, "%.*g", DBL_DIG, num);
        jsrPushStringSz(vm, string, written);
    }
    return true;
}

JSR_NATIVE(jsr_Number_hash) {
    jsrPushNumber(vm, hashNumber(AS_NUM(vm->apiStack[0])));
    return true;
}
// end

// class Boolean
JSR_NATIVE(jsr_Boolean_construct) {
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
    Obj* fn = AS_OBJ(vm->apiStack[0]);
    Prototype* proto = getPrototype(fn);

    const char* fnType = NULL;

    switch(fn->type) {
    case OBJ_CLOSURE:
        fnType = "function";
        break;
    case OBJ_NATIVE:
        fnType = "native";
        break;
    case OBJ_BOUND_METHOD:
        fnType = "bound method";
        break;
    default:
        JSR_UNREACHABLE();
    }

    JStarBuffer str;
    jsrBufferInit(vm, &str);

    if(strcmp(proto->module->name->data, JSR_CORE_MODULE) == 0) {
        jsrBufferAppendf(&str, "<%s %s@%p>", fnType, proto->name->data, (void*)fn);
    } else {
        jsrBufferAppendf(&str, "<%s %s.%s@%p>", fnType, proto->module->name->data,
                         proto->name->data, (void*)fn);
    }

    jsrBufferPush(&str);
    return true;
}

static bool checkBuiltin(JStarVM* vm, ObjClass* cls) {
    return vm->clsClass == cls || vm->objClass == cls || vm->strClass == cls ||
           vm->boolClass == cls || vm->lstClass == cls || vm->numClass == cls ||
           vm->funClass == cls || vm->genClass == cls || vm->modClass == cls ||
           vm->nullClass == cls || vm->stClass == cls || vm->tupClass == cls ||
           vm->excClass == cls || vm->tableClass == cls || vm->udataClass == cls;
}

JSR_NATIVE(jsr_Function_bind) {
    Obj* fn = AS_OBJ(vm->apiStack[0]);

    if(fn->type == OBJ_BOUND_METHOD) {
        ObjBoundMethod* bm = (ObjBoundMethod*)fn;
        if(checkBuiltin(vm, getClass(vm, bm->receiver))) {
            JSR_RAISE(vm, "TypeException", "Cannot bind built-in class method %s",
                      getPrototype(bm->method)->name->data);
        }
        fn = bm->method;
    }

    ObjBoundMethod* bound = newBoundMethod(vm, vm->apiStack[1], fn);
    push(vm, OBJ_VAL(bound));

    return true;
}

JSR_NATIVE(jsr_Function_arity) {
    Obj* fn = AS_OBJ(vm->apiStack[0]);
    Prototype* prototype = getPrototype(fn);
    jsrPushNumber(vm, prototype->argsCount);
    return true;
}

JSR_NATIVE(jsr_Function_vararg) {
    Obj* fn = AS_OBJ(vm->apiStack[0]);
    Prototype* prototype = getPrototype(fn);
    jsrPushBoolean(vm, prototype->vararg);
    return true;
}

JSR_NATIVE(jsr_Function_defaults) {
    Obj* fn = AS_OBJ(vm->apiStack[0]);
    Prototype* prototype = getPrototype(fn);
    ObjTuple* defaultTuple = newTuple(vm, prototype->defCount);
    push(vm, OBJ_VAL(defaultTuple));
    if(prototype->defCount) {
        memcpy(defaultTuple->arr, prototype->defaults, prototype->defCount * sizeof(Value));
    }
    return true;
}

JSR_NATIVE(jsr_Function_getName) {
    Obj* fn = AS_OBJ(vm->apiStack[0]);
    Prototype* prototype = getPrototype(fn);
    ObjModule* mod = prototype->module;

    JStarBuffer buf;
    jsrBufferInitCapacity(vm, &buf, prototype->name->length + mod->name->length + 1);
    jsrBufferAppendf(&buf, "%s.%s", mod->name->data, prototype->name->data);
    jsrBufferPush(&buf);

    return true;
}

JSR_NATIVE(jsr_Function_getSimpleName) {
    Obj* fn = AS_OBJ(vm->apiStack[0]);
    Prototype* prototype = getPrototype(fn);
    push(vm, OBJ_VAL(prototype->name));
    return true;
}
// end

// class Generator
JSR_NATIVE(jsr_Generator_isDone) {
    ObjGenerator* gen = AS_GENERATOR(vm->apiStack[0]);
    push(vm, BOOL_VAL(gen->state == GEN_DONE));
    return true;
}

JSR_NATIVE(jsr_Generator_string) {
    ObjGenerator* gen = AS_GENERATOR(vm->apiStack[0]);
    const Prototype* proto = &gen->closure->fn->proto;
    JStarBuffer str;
    jsrBufferInit(vm, &str);
    jsrBufferAppendf(&str, "<Generator %s.%s@%p>", proto->module->name->data, proto->name->data,
                     (void*)gen);
    jsrBufferPush(&str);
    return true;
}

JSR_NATIVE(jsr_Generator_next) {
    ObjGenerator* gen = AS_GENERATOR(vm->apiStack[0]);
    push(vm, gen->lastYield);
    return true;
}
// end

// class Module
JSR_NATIVE(jsr_Module_string) {
    ObjModule* m = AS_MODULE(vm->apiStack[0]);
    JStarBuffer str;
    jsrBufferInit(vm, &str);
    jsrBufferAppendf(&str, "<module %s@%s>", m->name->data, m->path->data);
    jsrBufferPush(&str);
    return true;
}

JSR_NATIVE(jsr_Module_globals) {
    ObjModule* module = AS_MODULE(vm->apiStack[0]);
    const IntHashTable* globalNames = &module->globalNames;

    jsrPushTable(vm);
    for(const IntEntry* e = globalNames->entries;
        e < globalNames->entries + globalNames->sizeMask + 1; e++) {
        if(e->key) {
            push(vm, OBJ_VAL(e->key));
            push(vm, module->globals[e->value]);
            if(!jsrSubscriptSet(vm, -3)) return false;
            pop(vm);
        }
    }

    return true;
}
// end

// class List
JSR_NATIVE(jsr_List_construct) {
    if(jsrIsNull(vm, 1)) {
        jsrPushList(vm);
    } else if(jsrIsInteger(vm, 1)) {
        double count = jsrGetNumber(vm, 1);

        if(count < 0) {
            JSR_RAISE(vm, "TypeException", "size must be >= 0");
        }

        ObjList* lst = newList(vm, count);
        push(vm, OBJ_VAL(lst));

        if(jsrIsFunction(vm, 2)) {
            for(size_t i = 0; i < count; i++) {
                jsrPushValue(vm, 2);
                jsrPushNumber(vm, i);
                if(jsrCall(vm, 1) != JSR_SUCCESS) return false;
                lst->arr[lst->size++] = pop(vm);
            }
        } else {
            for(size_t i = 0; i < count; i++) {
                lst->arr[lst->size++] = vm->apiStack[2];
            }
        }
    } else {
        JSR_CHECK(Null, 2, "when calling List with an Iterable init");
        jsrPushList(vm);
        JSR_FOREACH(
            1, {
                jsrListAppend(vm, 3);
                jsrPop(vm);
            }, )
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
    size_t index = jsrCheckIndex(vm, 1, l->size + 1, "i");
    if(index == SIZE_MAX) return false;

    listInsert(vm, l, index, vm->apiStack[2]);
    jsrPushNull(vm);
    return true;
}

JSR_NATIVE(jsr_List_len) {
    push(vm, NUM_VAL(AS_LIST(vm->apiStack[0])->size));
    return true;
}

JSR_NATIVE(jsr_List_plus) {
    JSR_CHECK(List, 1, "other");

    ObjList* lst1 = AS_LIST(vm->apiStack[0]);
    ObjList* lst2 = AS_LIST(vm->apiStack[1]);

    ObjList* concat = newList(vm, lst1->size + lst2->size);
    memcpy(concat->arr, lst1->arr, lst1->size * sizeof(Value));
    memcpy(concat->arr + lst1->size, lst2->arr, lst2->size * sizeof(Value));
    concat->size = concat->capacity;

    push(vm, OBJ_VAL(concat));
    return true;
}

JSR_NATIVE(jsr_List_eq) {
    ObjList* lst = AS_LIST(vm->apiStack[0]);

    if(!IS_LIST(vm->apiStack[1])) {
        jsrPushBoolean(vm, false);
        return true;
    }

    ObjList* other = AS_LIST(vm->apiStack[1]);

    if(other->size != lst->size) {
        jsrPushBoolean(vm, false);
        return true;
    }

    bool res = false;
    if(!compareValues(vm, lst->arr, other->arr, lst->size, &res)) return false;

    jsrPushBoolean(vm, res);
    return true;
}

JSR_NATIVE(jsr_List_removeAt) {
    ObjList* l = AS_LIST(vm->apiStack[0]);
    size_t index = jsrCheckIndex(vm, 1, l->size, "i");
    if(index == SIZE_MAX) return false;

    Value r = l->arr[index];
    listRemove(vm, l, index);
    push(vm, r);
    return true;
}

JSR_NATIVE(jsr_List_clear) {
    AS_LIST(vm->apiStack[0])->size = 0;
    jsrPushNull(vm);
    return true;
}

typedef struct MergeState {
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

        if(jsrCallMethod(vm, "__le__", 1) != JSR_SUCCESS) {
            jsrPop(vm);
            jsrRaise(vm, "TypeException", "Operator <= not defined for type %s, %s",
                     getClass(vm, a)->name->data, getClass(vm, b)->name->data);
            return false;
        }

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
    if(!mergeSort(vm, list->arr, list->size, comp)) return false;
    jsrPushNull(vm);
    return true;
}

JSR_NATIVE(jsr_List_iter) {
    ObjList* lst = AS_LIST(vm->apiStack[0]);

    if(IS_NULL(vm->apiStack[1]) && lst->size != 0) {
        push(vm, NUM_VAL(0));
        return true;
    }

    if(IS_NUM(vm->apiStack[1])) {
        size_t idx = (size_t)AS_NUM(vm->apiStack[1]);
        if(idx < lst->size - 1) {
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
        if(idx < lst->size) {
            push(vm, lst->arr[idx]);
            return true;
        }
    }

    push(vm, NULL_VAL);
    return true;
}
// end

// class Tuple
JSR_NATIVE(jsr_Tuple_construct) {
    if(IS_NULL(vm->apiStack[1])) {
        push(vm, OBJ_VAL(newTuple(vm, 0)));
        return true;
    }

    // If provided input is another tuple, return that tuple
    if(IS_TUPLE(vm->apiStack[1])) {
        push(vm, vm->apiStack[1]);
        return true;
    }

    // Consume the iterable into list
    if(!IS_LIST(vm->apiStack[1])) {
        jsrPushList(vm);
        JSR_FOREACH(
            1, {
                jsrListAppend(vm, 2);
                jsrPop(vm);
            }, )
    }

    // Convert the list to a tuple
    ObjList* list = AS_LIST(vm->sp[-1]);
    ObjTuple* tuple = newTuple(vm, list->size);

    if(list->size > 0) {
        memcpy(tuple->arr, list->arr, sizeof(Value) * list->size);
    }

    push(vm, OBJ_VAL(tuple));
    return true;
}

JSR_NATIVE(jsr_Tuple_len) {
    push(vm, NUM_VAL(AS_TUPLE(vm->apiStack[0])->size));
    return true;
}

JSR_NATIVE(jsr_Tuple_add) {
    JSR_CHECK(Tuple, 1, "other");

    ObjTuple* tup1 = AS_TUPLE(vm->apiStack[0]);
    ObjTuple* tup2 = AS_TUPLE(vm->apiStack[1]);

    ObjTuple* concat = newTuple(vm, tup1->size + tup2->size);
    memcpy(concat->arr, tup1->arr, tup1->size * sizeof(Value));
    memcpy(concat->arr + tup1->size, tup2->arr, tup2->size * sizeof(Value));

    push(vm, OBJ_VAL(concat));
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
JSR_NATIVE(jsr_String_construct) {
    JStarBuffer stringBuf;
    jsrBufferInit(vm, &stringBuf);

    JSR_FOREACH(
        1,
        {
            if(jsrCallMethod(vm, "__string__", 0) != JSR_SUCCESS) {
                jsrBufferFree(&stringBuf);
                return false;
            }
            if(!jsrIsString(vm, -1)) {
                jsrBufferFree(&stringBuf);
                JSR_RAISE(vm, "TypeException", "__string__() didn't return a String");
            }
            jsrBufferAppendStr(&stringBuf, jsrGetString(vm, -1));
            jsrPop(vm);
        },
        jsrBufferFree(&stringBuf));

    jsrBufferPush(&stringBuf);
    return true;
}

JSR_NATIVE(jsr_String_findSubstr) {
    JSR_CHECK(String, 1, "substring");
    if(!jsrIsNull(vm, 2)) {
        JSR_CHECK(Int, 2, "start");
    }
    if(!jsrIsNull(vm, 3)) {
        JSR_CHECK(Int, 3, "stop");
    }

    const char* thisStr = jsrGetString(vm, 0);
    size_t thisLen = jsrGetStringSz(vm, 0);
    const char* substring = jsrGetString(vm, 1);
    size_t substringLen = jsrGetStringSz(vm, 1);
    double start = jsrIsNull(vm, 2) ? 0 : jsrGetNumber(vm, 2);
    double stop = jsrIsNull(vm, 3) ? thisLen : jsrGetNumber(vm, 3);

    if(start < 0) {
        JSR_RAISE(vm, "InvalidArgException", "start must be >= 0");
    }
    if(stop > thisLen) {
        JSR_RAISE(vm, "InvalidArgException", "stop must be <= the length of the string");
    }
    if(start > stop) {
        JSR_RAISE(vm, "InvalidArgException", "start must be <= stop");
    }

    for(size_t i = start; i <= stop - substringLen; i++) {
        if(memcmp(thisStr + i, substring, substringLen) == 0) {
            jsrPushNumber(vm, i);
            return true;
        }
    }

    jsrPushNumber(vm, -1);
    return true;
}

JSR_NATIVE(jsr_String_rfindSubstr) {
    JSR_CHECK(String, 1, "substring");
    if(!jsrIsNull(vm, 2)) {
        JSR_CHECK(Int, 2, "start");
    }
    if(!jsrIsNull(vm, 3)) {
        JSR_CHECK(Int, 3, "stop");
    }

    const char* thisStr = jsrGetString(vm, 0);
    size_t thisLen = jsrGetStringSz(vm, 0);
    const char* substring = jsrGetString(vm, 1);
    size_t substringLen = jsrGetStringSz(vm, 1);
    double start = jsrIsNull(vm, 2) ? 0 : jsrGetNumber(vm, 2);
    double stop = jsrIsNull(vm, 3) ? thisLen : jsrGetNumber(vm, 3);

    if(start < 0) {
        JSR_RAISE(vm, "InvalidArgException", "start must be >= 0");
    }
    if(stop > thisLen) {
        JSR_RAISE(vm, "InvalidArgException", "stop must be <= the length of the string");
    }
    if(start > stop) {
        JSR_RAISE(vm, "InvalidArgException", "start must be <= stop");
    }

    for(size_t i = stop - substringLen; i != (size_t)(start - 1); i--) {
        if(memcmp(thisStr + i, substring, substringLen) == 0) {
            jsrPushNumber(vm, i);
            return true;
        }
    }

    jsrPushNumber(vm, -1);
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

JSR_NATIVE(jsr_String_split) {
    JSR_CHECK(String, 1, "delimiter");

    const char* str = jsrGetString(vm, 0);
    size_t size = jsrGetStringSz(vm, 0);

    const char* delimiter = jsrGetString(vm, 1);
    size_t delimSize = jsrGetStringSz(vm, 1);
    if(delimSize == 0) JSR_RAISE(vm, "InvalidArgException", "Empty delimiter");

    ObjList* tokens = newList(vm, 0);
    push(vm, OBJ_VAL(tokens));

    const char* last = str;

    if(delimSize < size) {
        size_t i = 0;
        while(i <= size - delimSize) {
            if(memcmp(str + i, delimiter, delimSize) == 0) {
                jsrPushStringSz(vm, last, str + i - last);
                jsrListAppend(vm, -2);
                jsrPop(vm);

                last = str + i + delimSize;
                i += delimSize;
            } else {
                i++;
            }
        }
    }

    jsrPushStringSz(vm, last, str + size - last);
    jsrListAppend(vm, -2);
    jsrPop(vm);

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

JSR_NATIVE(jsr_String_escaped) {
    const char* str = jsrGetString(vm, 0);
    size_t size = jsrGetStringSz(vm, 0);

    const int numEscapes = 10;
    const char* escaped = "\0\a\b\f\n\r\t\v\\\"";
    const char* unescaped = "0abfnrtv\\\"";

    JStarBuffer buf;
    jsrBufferInitCapacity(vm, &buf, size * 1.5);
    for(size_t i = 0; i < size; i++) {
        int j;
        for(j = 0; j < numEscapes; j++) {
            if(str[i] == escaped[j]) {
                jsrBufferAppendChar(&buf, '\\');
                jsrBufferAppendChar(&buf, unescaped[j]);
                break;
            }
        }

        if(j == numEscapes) {
            jsrBufferAppendChar(&buf, str[i]);
        }
    }

    jsrBufferPush(&buf);
    return true;
}

JSR_NATIVE(jsr_String_mul) {
    JSR_CHECK(Int, 1, "reps");

    size_t size = jsrGetStringSz(vm, 0);
    double reps = jsrGetNumber(vm, -1);
    if(reps < 0) reps = 0;

    JStarBuffer repeated;
    jsrBufferInitCapacity(vm, &repeated, reps * size);

    for(size_t i = 0; i < reps; i++) {
        jsrBufferAppend(&repeated, jsrGetString(vm, 0), jsrGetStringSz(vm, 0));
    }

    jsrBufferPush(&repeated);
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
    jsrPushNumber(vm, stringGetHash(AS_STRING(vm->apiStack[0])));
    return true;
}

JSR_NATIVE(jsr_String_eq) {
    if(!jsrIsString(vm, 1)) {
        jsrPushBoolean(vm, false);
        return true;
    }

    ObjString* s1 = AS_STRING(vm->apiStack[0]);
    ObjString* s2 = AS_STRING(vm->apiStack[1]);

    jsrPushBoolean(vm, stringEquals(s1, s2));
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
#define TOMB_MARKER      TRUE_VAL
#define INITIAL_CAPACITY 8
#define GROW_FACTOR      2

static bool tableKeyHash(JStarVM* vm, Value key, uint32_t* hash) {
    if(IS_STRING(key)) {
        *hash = stringGetHash(AS_STRING(key));
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
    *hash = (uint32_t)AS_NUM(pop(vm));

    return hash;
}

static bool tableKeyEquals(JStarVM* vm, Value k1, Value k2, bool* eq) {
    if(IS_NUM(k1) || IS_BOOL(k1)) {
        *eq = valueEquals(k1, k2);
        return true;
    }
    if(IS_STRING(k1) && IS_STRING(k2)) {
        *eq = stringEquals(AS_STRING(k1), AS_STRING(k2));
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
                *out = tomb ? tomb : e;
                return true;
            } else if(!tomb) {
                tomb = e;
            }
        } else {
            bool eq;
            if(!tableKeyEquals(vm, key, e->key, &eq)) {
                return false;
            }

            if(eq) {
                *out = e;
                return true;
            }
        }
        i = (i + 1) & sizeMask;
    }
}

static void growEntries(JStarVM* vm, ObjTable* t) {
    size_t newCap = t->capacityMask ? (t->capacityMask + 1) * GROW_FACTOR : INITIAL_CAPACITY;
    TableEntry* newEntries = GC_ALLOC(vm, sizeof(TableEntry) * newCap);
    for(size_t i = 0; i < newCap; i++) {
        newEntries[i] = (TableEntry){NULL_VAL, NULL_VAL};
    }

    t->numEntries = 0, t->size = 0;
    if(t->capacityMask != 0) {
        for(size_t i = 0; i <= t->capacityMask; i++) {
            TableEntry* e = &t->entries[i];
            if(IS_NULL(e->key)) continue;

            TableEntry* dest;
            findEntry(vm, newEntries, newCap - 1, e->key, &dest);
            *dest = (TableEntry){e->key, e->val};
            t->numEntries++, t->size++;
        }
        GC_FREE_ARRAY(vm, TableEntry, t->entries, t->capacityMask + 1);
    }
    t->entries = newEntries;
    t->capacityMask = newCap - 1;
}

JSR_NATIVE(jsr_Table_construct) {
    ObjTable* table = newTable(vm);
    push(vm, OBJ_VAL(table));

    if(IS_TABLE(vm->apiStack[1]) && AS_TABLE(vm->apiStack[1])->size) {
        ObjTable* other = AS_TABLE(vm->apiStack[1]);
        for(size_t i = 0; i <= other->capacityMask; i++) {
            TableEntry* e = &other->entries[i];
            if(!IS_NULL(e->key)) {
                push(vm, OBJ_VAL(table));
                push(vm, e->key);
                push(vm, e->val);
                if(jsrCallMethod(vm, "__set__", 2) != JSR_SUCCESS) return false;
                pop(vm);
            }
        }
    } else if(!IS_NULL(vm->apiStack[1])) {
        JSR_FOREACH(
            1, {
                if(!IS_LIST(peek(vm)) && !IS_TUPLE(peek(vm))) {
                    JSR_RAISE(vm, "TypeException",
                              "Iterable elements in table costructor must be either a List or a "
                              "Tuple, got %s",
                              getClass(vm, peek(vm))->name->data);
                }

                size_t size;
                Value* array = getValues(AS_OBJ(peek(vm)), &size);

                if(size != 2) {
                    JSR_RAISE(vm, "TypeException", "Iterable element of length %zu, must be 2",
                              size);
                }

                push(vm, OBJ_VAL(table));
                push(vm, array[0]);
                push(vm, array[1]);

                if(jsrCallMethod(vm, "__set__", 2) != JSR_SUCCESS) return false;

                pop(vm);
                pop(vm);
            }, )
    }

    return true;
}

JSR_NATIVE(jsr_Table_get) {
    if(jsrIsNull(vm, 1)) JSR_RAISE(vm, "TypeException", "Key of Table cannot be null.");

    ObjTable* t = AS_TABLE(vm->apiStack[0]);
    if(t->entries == NULL) {
        push(vm, NULL_VAL);
        return true;
    }

    TableEntry* e;
    if(!findEntry(vm, t->entries, t->capacityMask, vm->apiStack[1], &e)) {
        return false;
    }

    if(!IS_NULL(e->key)) {
        push(vm, e->val);
    } else {
        push(vm, NULL_VAL);
    }

    return true;
}

static size_t tableMaxEntryLoad(size_t capacity) {
    return (capacity >> 1) + (capacity >> 2);  // Read as: 3/4 * capacity i.e. a load factor of 75%
}

JSR_NATIVE(jsr_Table_set) {
    if(jsrIsNull(vm, 1)) JSR_RAISE(vm, "TypeException", "Key of Table cannot be null.");

    ObjTable* t = AS_TABLE(vm->apiStack[0]);
    if(t->numEntries + 1 > tableMaxEntryLoad(t->capacityMask + 1)) {
        growEntries(vm, t);
    }

    TableEntry* e;
    if(!findEntry(vm, t->entries, t->capacityMask, vm->apiStack[1], &e)) {
        return false;
    }

    bool newEntry = IS_NULL(e->key);
    if(newEntry) {
        t->size++;
        if(IS_NULL(e->val)) t->numEntries++;
    }

    *e = (TableEntry){vm->apiStack[1], vm->apiStack[2]};
    push(vm, BOOL_VAL(newEntry));
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
    if(!findEntry(vm, t->entries, t->capacityMask, vm->apiStack[1], &toDelete)) {
        return false;
    }

    if(IS_NULL(toDelete->key)) {
        jsrPushBoolean(vm, false);
        return true;
    }

    *toDelete = (TableEntry){NULL_VAL, TOMB_MARKER};
    t->size--;

    push(vm, BOOL_VAL(true));
    return true;
}

JSR_NATIVE(jsr_Table_clear) {
    ObjTable* t = AS_TABLE(vm->apiStack[0]);
    t->numEntries = t->size = 0;
    for(size_t i = 0; i < t->capacityMask + 1; i++) {
        t->entries[i] = (TableEntry){NULL_VAL, NULL_VAL};
    }
    push(vm, NULL_VAL);
    return true;
}

JSR_NATIVE(jsr_Table_len) {
    ObjTable* t = AS_TABLE(vm->apiStack[0]);
    push(vm, NUM_VAL(t->size));
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
    if(!findEntry(vm, t->entries, t->capacityMask, vm->apiStack[1], &e)) {
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
        for(size_t i = 0; i < t->capacityMask + 1; i++) {
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
        for(size_t i = 0; i < t->capacityMask + 1; i++) {
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
        if(idx >= t->capacityMask) {
            push(vm, BOOL_VAL(false));
            return true;
        }
        lastIdx = idx + 1;
    }

    for(size_t i = lastIdx; i < t->capacityMask + 1; i++) {
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
        if(idx <= t->capacityMask) {
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
        for(size_t i = 0; i < t->capacityMask + 1; i++) {
            if(IS_NULL(entries[i].key)) continue;

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
        jsrBufferTrunc(&buf, buf.size - 2);
    }
    jsrBufferAppendChar(&buf, '}');
    jsrBufferPush(&buf);
    return true;
}
// end

// class Enum
#define M_VALUE_NAME "_valueName"

static bool checkEnumElem(JStarVM* vm, ObjClass* cls, ObjInstance* inst) {
    if(!IS_STRING(peek(vm))) {
        JSR_RAISE(vm, "TypeException", "Enum element must be a String, got %s",
                  getClass(vm, peek(vm))->name->data);
    }

    ObjString* enumElem = AS_STRING(peek(vm));
    if(isalpha(enumElem->data[0])) {
        for(size_t i = 1; i < enumElem->length; i++) {
            char c = enumElem->data[i];
            if(!isalpha(c) && !isdigit(c) && c != '_') {
                JSR_RAISE(vm, "InvalidArgException", "Enum element `%s` is not a valid identifier",
                          enumElem->data);
            }
        }

        Value val;
        if(instanceGetField(vm, cls, inst, enumElem, &val)) {
            JSR_RAISE(vm, "InvalidArgException", "Duplicate Enum element `%s`", enumElem->data);
        }

        return true;
    }

    JSR_RAISE(vm, "InvalidArgException", "Enum element `%s` is not a valid identifier",
              enumElem->data);
}

JSR_NATIVE(jsr_Enum_construct) {
    ObjInstance* inst = AS_INSTANCE(vm->apiStack[0]);
    ObjClass* cls = inst->base.cls;

    if(jsrTupleGetLength(vm, 1) == 0) {
        JSR_RAISE(vm, "InvalidArgException", "Cannot create empty Enum");
    }

    jsrPushTable(vm);
    jsrSetField(vm, 0, M_VALUE_NAME);
    jsrPop(vm);

    jsrTupleGet(vm, 0, 1);
    bool isCustom = jsrIsTable(vm, -1);

    if(!isCustom) {
        jsrPop(vm);
        jsrPushValue(vm, 1);
    }

    int iota = 0;
    JSR_FOREACH(
        2, {
            if(!checkEnumElem(vm, cls, inst)) return false;

            if(isCustom) {
                jsrPushValue(vm, -1);
                if(!jsrSubscriptGet(vm, 2)) return false;
            } else {
                jsrPushNumber(vm, iota);
            }

            jsrSetField(vm, 0, jsrGetString(vm, -2));

            jsrGetField(vm, 0, M_VALUE_NAME);
            jsrPushValue(vm, -2);
            jsrPushValue(vm, -4);
            if(!jsrSubscriptSet(vm, -3)) return false;
            jsrPop(vm);
            jsrPop(vm);

            jsrPop(vm);
            jsrPop(vm);

            iota++;
        }, );

    if(iota == 0) {
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
