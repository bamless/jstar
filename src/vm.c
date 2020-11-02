#include "vm.h"

#include <math.h>
#include <string.h>

#include "code.h"
#include "gc.h"
#include "import.h"
#include "opcode.h"
#include "std/core.h"
#include "std/modules.h"

// Method names of overloadable operators
static const char* overloadNames[OVERLOAD_SENTINEL] = {
    [ADD_OVERLOAD] = "__add__",   [SUB_OVERLOAD] = "__sub__",   [MUL_OVERLOAD] = "__mul__",
    [DIV_OVERLOAD] = "__div__",   [MOD_OVERLOAD] = "__mod__",   [RADD_OVERLOAD] = "__radd__",
    [RSUB_OVERLOAD] = "__rsub__", [RMUL_OVERLOAD] = "__rmul__", [RDIV_OVERLOAD] = "__rdiv__",
    [RMOD_OVERLOAD] = "__rmod__", [GET_OVERLOAD] = "__get__",   [SET_OVERLOAD] = "__set__",
    [EQ_OVERLOAD] = "__eq__",     [LT_OVERLOAD] = "__lt__",     [LE_OVERLOAD] = "__le__",
    [GT_OVERLOAD] = "__gt__",     [GE_OVERLOAD] = "__ge__",     [NEG_OVERLOAD] = "__neg__",
};

// -----------------------------------------------------------------------------
// VM INITIALIZATION AND DESTRUCTION
// -----------------------------------------------------------------------------

static void resetStack(JStarVM* vm) {
    vm->sp = vm->stack;
    vm->apiStack = vm->stack;
    vm->frameCount = 0;
    vm->module = NULL;
}

static void initConstStrings(JStarVM* vm) {
    for(int i = 0; i < OVERLOAD_SENTINEL; i++) {
        vm->overloads[i] = copyString(vm, overloadNames[i], strlen(overloadNames[i]));
    }
    vm->ctor = copyString(vm, CTOR_STR, strlen(CTOR_STR));
    vm->next = copyString(vm, NEXT_METH, strlen(NEXT_METH));
    vm->iter = copyString(vm, ITER_METH, strlen(ITER_METH));
}

static void initMainModule(JStarVM* vm) {
    ObjString* mainModuleName = copyString(vm, JSR_MAIN_MODULE, strlen(JSR_MAIN_MODULE));
    compileWithModule(vm, "<main>", mainModuleName, NULL);
}

JStarVM* jsrNewVM(const JStarConf* conf) {
    JStarVM* vm = calloc(1, sizeof(*vm));
    vm->errorCallback = conf->errorCallback;

    // VM program stack
    vm->stackSz = roundUp(conf->stackSize, MAX_LOCALS + 1);
    vm->frameSz = vm->stackSz / (MAX_LOCALS + 1);
    vm->stack = malloc(sizeof(Value) * vm->stackSz);
    vm->frames = malloc(sizeof(Frame) * vm->frameSz);
    resetStack(vm);

    // GC Values
    vm->nextGC = conf->initGC;
    vm->heapGrowRate = conf->heapGrowRate;

    // Module and String caches
    initHashTable(&vm->modules);
    initHashTable(&vm->strings);

    initConstStrings(vm);

    // Core module bootstrap
    initCoreModule(vm);
    // Init empty main module
    initMainModule(vm);

    // Create J* objects needed by the runtime. This is called after initCoreLibrary in order
    // to correctly assign a Class reference to the objects, since built-in classes are created
    // during the core module initialization
    vm->importpaths = newList(vm, 0);
    vm->emptyTup = newTuple(vm, 0);

    return vm;
}

void jsrFreeVM(JStarVM* vm) {
    resetStack(vm);

    free(vm->stack);
    free(vm->frames);
    freeHashTable(&vm->strings);
    freeHashTable(&vm->modules);
    freeObjects(vm);

#ifdef JSTAR_DBG_PRINT_GC
    printf("Allocated at exit: %lu bytes.\n", vm->allocated);
#endif

    free(vm);
}

// -----------------------------------------------------------------------------
// VM IMPLEMENTATION
// -----------------------------------------------------------------------------

static Frame* getFrame(JStarVM* vm, FnCommon* c) {
    if(vm->frameCount + 1 == vm->frameSz) {
        vm->frameSz *= 2;
        vm->frames = realloc(vm->frames, sizeof(Frame) * vm->frameSz);
    }

    Frame* callFrame = &vm->frames[vm->frameCount++];
    callFrame->stack = vm->sp - (c->argsCount + 1) - (int)c->vararg;
    callFrame->handlerc = 0;
    return callFrame;
}

static Frame* appendCallFrame(JStarVM* vm, ObjClosure* closure) {
    Frame* callFrame = getFrame(vm, &closure->fn->c);
    callFrame->fn = (Obj*)closure;
    callFrame->ip = closure->fn->code.bytecode;
    return callFrame;
}

static Frame* appendNativeFrame(JStarVM* vm, ObjNative* native) {
    Frame* callFrame = getFrame(vm, &native->c);
    callFrame->fn = (Obj*)native;
    callFrame->ip = NULL;
    return callFrame;
}

static bool isNonInstantiableBuiltin(JStarVM* vm, ObjClass* cls) {
    return cls == vm->nullClass || cls == vm->funClass || cls == vm->modClass ||
           cls == vm->stClass || cls == vm->clsClass || cls == vm->tableClass ||
           cls == vm->udataClass;
}

static bool isInstatiableBuiltin(JStarVM* vm, ObjClass* cls) {
    return cls == vm->lstClass || cls == vm->tupClass || cls == vm->numClass ||
           cls == vm->boolClass || cls == vm->strClass;
}

static bool isBuiltinClass(JStarVM* vm, ObjClass* cls) {
    return isNonInstantiableBuiltin(vm, cls) || isInstatiableBuiltin(vm, cls);
}

static bool isInt(double n) {
    return trunc(n) == n;
}

static void swapValues(Value* valueArray, int a, int b) {
    Value tmp = valueArray[a];
    valueArray[a] = valueArray[b];
    valueArray[b] = tmp;
}

static void createClass(JStarVM* vm, ObjString* name, ObjClass* superCls) {
    ObjClass* cls = newClass(vm, name, superCls);
    hashTableMerge(&cls->methods, &superCls->methods);
    push(vm, OBJ_VAL(cls));
}

static ObjUpvalue* captureUpvalue(JStarVM* vm, Value* addr) {
    if(vm->upvalues == NULL) {
        vm->upvalues = newUpvalue(vm, addr);
        return vm->upvalues;
    }

    ObjUpvalue* prev = NULL;
    ObjUpvalue* upvalue = vm->upvalues;

    while(upvalue != NULL && upvalue->addr > addr) {
        prev = upvalue;
        upvalue = upvalue->next;
    }

    if(upvalue != NULL && upvalue->addr == addr) return upvalue;

    ObjUpvalue* createdUpvalue = newUpvalue(vm, addr);
    if(prev == NULL) {
        vm->upvalues = createdUpvalue;
    } else {
        prev->next = createdUpvalue;
    }

    createdUpvalue->next = upvalue;
    return createdUpvalue;
}

static void closeUpvalues(JStarVM* vm, Value* last) {
    while(vm->upvalues != NULL && vm->upvalues->addr >= last) {
        ObjUpvalue* upvalue = vm->upvalues;
        upvalue->closed = *upvalue->addr;
        upvalue->addr = &upvalue->closed;
        vm->upvalues = upvalue->next;
    }
}

static void packVarargs(JStarVM* vm, uint8_t count) {
    ObjTuple* args = newTuple(vm, count);
    for(int i = count - 1; i >= 0; i--) {
        args->arr[i] = pop(vm);
    }
    push(vm, OBJ_VAL(args));
}

static void argumentError(JStarVM* vm, FnCommon* c, int expected, int supplied,
                          const char* quantity) {
    jsrRaise(vm, "TypeException", "Function `%s.%s` takes %s %d arguments, %d supplied.",
             c->module->name->data, c->name->data, quantity, expected, supplied);
}

static bool adjustArguments(JStarVM* vm, FnCommon* c, uint8_t argc) {
    bool isVararg = c->vararg;
    uint8_t most = c->argsCount, least = most - c->defCount;

    if(!isVararg && argc > most) {
        argumentError(vm, c, c->argsCount, argc, most == least ? "exactly" : "at most");
        return false;
    } else if(argc < least) {
        argumentError(vm, c, least, argc, (most == least && !isVararg) ? "exactly" : "at least");
        return false;
    }

    // push remaining args taking the default value
    for(uint8_t i = argc - least; i < c->defCount; i++) {
        push(vm, c->defaults[i]);
    }

    if(isVararg) {
        packVarargs(vm, argc > most ? argc - most : 0);
    }

    return true;
}

static bool callFunction(JStarVM* vm, ObjClosure* closure, uint8_t argc) {
    if(vm->frameCount + 1 == RECURSION_LIMIT) {
        jsrRaise(vm, "StackOverflowException", NULL);
        return false;
    }

    if(!adjustArguments(vm, &closure->fn->c, argc)) {
        return false;
    }

    // TODO: modify compiler to track actual usage of stack so
    // we can allocate the right amount of memory rather than a
    // worst case bound
    ensureStack(vm, UINT8_MAX);
    appendCallFrame(vm, closure);
    vm->module = closure->fn->c.module;

    return true;
}

static bool callNative(JStarVM* vm, ObjNative* native, uint8_t argc) {
    if(vm->frameCount + 1 == RECURSION_LIMIT) {
        jsrRaise(vm, "StackOverflowException", NULL);
        return false;
    }

    if(!adjustArguments(vm, &native->c, argc)) {
        return false;
    }

    ensureStack(vm, JSTAR_MIN_NATIVE_STACK_SZ);
    Frame* frame = appendNativeFrame(vm, native);

    ObjModule* oldModule = vm->module;
    size_t apiStackOffset = vm->apiStack - vm->stack;

    vm->module = native->c.module;
    vm->apiStack = frame->stack;

    if(!native->fn(vm)) {
        vm->module = oldModule;
        vm->apiStack = vm->stack + apiStackOffset;
        return false;
    }

    Value ret = pop(vm);

    vm->frameCount--;
    vm->sp = vm->apiStack;
    vm->module = oldModule;
    vm->apiStack = vm->stack + apiStackOffset;

    push(vm, ret);
    return true;
}

static bool invokeMethod(JStarVM* vm, ObjClass* cls, ObjString* name, uint8_t argc) {
    Value method;
    if(!hashTableGet(&cls->methods, name, &method)) {
        jsrRaise(vm, "MethodException", "Method %s.%s() doesn't exists", cls->name->data,
                 name->data);
        return false;
    }
    return callValue(vm, method, argc);
}

static bool bindMethod(JStarVM* vm, ObjClass* cls, ObjString* name) {
    Value v;
    if(!hashTableGet(&cls->methods, name, &v)) {
        return false;
    }

    ObjBoundMethod* boundMeth = newBoundMethod(vm, peek(vm), AS_OBJ(v));
    pop(vm);

    push(vm, OBJ_VAL(boundMeth));
    return true;
}

static bool checkSliceIndex(JStarVM* vm, ObjTuple* slice, size_t size, size_t* low, size_t* high) {
    if(slice->size != 2) JSR_RAISE(vm, "TypeException", "Slice index must have two elements.");
    if(!IS_INT(slice->arr[0]) || !IS_INT(slice->arr[1])) {
        JSR_RAISE(vm, "TypeException", "Slice index must be two integers.");
    }

    size_t a = jsrCheckIndexNum(vm, AS_NUM(slice->arr[0]), size + 1);
    if(a == SIZE_MAX) return false;
    size_t b = jsrCheckIndexNum(vm, AS_NUM(slice->arr[1]), size + 1);
    if(b == SIZE_MAX) return false;

    if(a > b) {
        JSR_RAISE(vm, "InvalidArgException",
                  "Invalid slice indices (%g, %g), first must be <= than second",
                  AS_NUM(slice->arr[0]), AS_NUM(slice->arr[1]));
    }

    *low = a, *high = b;
    return true;
}

static bool getListSubscript(JStarVM* vm) {
    ObjList* lst = AS_LIST(peek2(vm));
    Value arg = peek(vm);

    if(IS_INT(arg)) {
        size_t idx = jsrCheckIndexNum(vm, AS_NUM(arg), lst->count);
        if(idx == SIZE_MAX) return false;

        pop(vm), pop(vm);
        push(vm, lst->arr[idx]);
        return true;
    }
    if(IS_TUPLE(arg)) {
        size_t low = 0, high = 0;
        if(!checkSliceIndex(vm, AS_TUPLE(arg), lst->count, &low, &high)) return false;

        ObjList* ret = newList(vm, high - low);
        ret->count = high - low;
        for(size_t i = low; i < high; i++) {
            ret->arr[i - low] = lst->arr[i];
        }

        pop(vm), pop(vm);
        push(vm, OBJ_VAL(ret));
        return true;
    }
    JSR_RAISE(vm, "TypeException", "Index of List subscript must be an integer or a Tuple");
}

static bool getTupleSubscript(JStarVM* vm) {
    ObjTuple* tup = AS_TUPLE(peek2(vm));
    Value arg = peek(vm);

    if(IS_INT(arg)) {
        size_t idx = jsrCheckIndexNum(vm, AS_NUM(arg), tup->size);
        if(idx == SIZE_MAX) return false;

        pop(vm), pop(vm);
        push(vm, tup->arr[idx]);
        return true;
    }
    if(IS_TUPLE(arg)) {
        size_t low = 0, high = 0;
        if(!checkSliceIndex(vm, AS_TUPLE(arg), tup->size, &low, &high)) return false;

        ObjTuple* ret = newTuple(vm, high - low);
        for(size_t i = low; i < high; i++) {
            ret->arr[i - low] = tup->arr[i];
        }

        pop(vm), pop(vm);
        push(vm, OBJ_VAL(ret));
        return true;
    }
    JSR_RAISE(vm, "TypeException", "Index of Tuple subscript must be an integer or a Tuple");
}

static bool getStringSubscript(JStarVM* vm) {
    ObjString* str = AS_STRING(peek2(vm));
    Value arg = peek(vm);

    if(IS_INT(arg)) {
        size_t idx = jsrCheckIndexNum(vm, AS_NUM(arg), str->length);
        if(idx == SIZE_MAX) return false;
        ObjString* ret = copyString(vm, str->data + idx, 1);

        pop(vm), pop(vm);
        push(vm, OBJ_VAL(ret));
        return true;
    }
    if(IS_TUPLE(arg)) {
        size_t low = 0, high = 0;
        if(!checkSliceIndex(vm, AS_TUPLE(arg), str->length, &low, &high)) return false;
        ObjString* ret = copyString(vm, str->data + low, high - low);

        pop(vm), pop(vm);
        push(vm, OBJ_VAL(ret));
        return true;
    }
    JSR_RAISE(vm, "TypeException", "Index of String subscript must be an integer or a Tuple");
}

static bool getSubscriptOfValue(JStarVM* vm) {
    if(IS_OBJ(peek2(vm))) {
        Value operand = peek2(vm);
        switch(OBJ_TYPE(operand)) {
        case OBJ_LIST:
            return getListSubscript(vm);
        case OBJ_TUPLE:
            return getTupleSubscript(vm);
        case OBJ_STRING:
            return getStringSubscript(vm);
        default:
            break;
        }
    }

    if(!invokeMethod(vm, getClass(vm, peek2(vm)), vm->overloads[GET_OVERLOAD], 1)) {
        return false;
    }
    return true;
}

static bool setSubscriptOfValue(JStarVM* vm) {
    if(IS_LIST(peek(vm))) {
        Value operand = pop(vm), arg = pop(vm), val = peek(vm);

        if(!IS_NUM(arg) || !isInt(AS_NUM(arg))) {
            jsrRaise(vm, "TypeException", "Index of List subscript access must be an integer.");
            return false;
        }

        ObjList* list = AS_LIST(operand);
        size_t index = jsrCheckIndexNum(vm, AS_NUM(arg), list->count);
        if(index == SIZE_MAX) return false;

        list->arr[index] = val;
        return true;
    }

    // swap the operand with value to prepare function call
    swapValues(vm->sp, -1, -3);
    if(!invokeMethod(vm, getClass(vm, peekn(vm, 2)), vm->overloads[SET_OVERLOAD], 2)) {
        return false;
    }
    return true;
}

static ObjString* stringConcatenate(JStarVM* vm, ObjString* s1, ObjString* s2) {
    size_t length = s1->length + s2->length;
    ObjString* str = allocateString(vm, length);
    memcpy(str->data, s1->data, s1->length);
    memcpy(str->data + s1->length, s2->data, s2->length);
    return str;
}

static bool callBinaryOverload(JStarVM* vm, const char* op, Overload overload, Overload reverse) {
    Value method;
    ObjClass* cls1 = getClass(vm, peek2(vm));
    ObjClass* cls2 = getClass(vm, peek(vm));

    if(hashTableGet(&cls1->methods, vm->overloads[overload], &method)) {
        return callValue(vm, method, 1);
    }

    if(reverse != OVERLOAD_SENTINEL) {
        swapValues(vm->sp, -1, -2);
        if(hashTableGet(&cls2->methods, vm->overloads[reverse], &method)) {
            return callValue(vm, method, 1);
        }
    }

    jsrRaise(vm, "TypeException", "Operator %s not defined for types %s, %s", op, cls1->name->data,
             cls2->name->data);
    return false;
}

static bool computeUnpackCallArgc(JStarVM* vm, int unpackArgc, uint8_t argc, uint8_t* out) {
    int callArgc = unpackArgc + argc;
    if(callArgc >= UINT8_MAX) {
        jsrRaise(vm, "TypeException", "Too many arguments for function call: %d", unpackArgc);
        return false;
    }
    *out = callArgc;
    return true;
}

static void getValueArray(Obj* obj, Value** array, size_t* size) {
    ASSERT(obj->type == OBJ_LIST || obj->type == OBJ_TUPLE, "Object isn't a tuple or list.");
    switch(obj->type) {
    case OBJ_TUPLE: {
        ObjTuple* tup = (ObjTuple*)obj;
        *array = tup->arr;
        *size = tup->size;
        break;
    }
    case OBJ_LIST: {
        ObjList* lst = (ObjList*)obj;
        *array = lst->arr;
        *size = lst->count;
        break;
    }
    default:
        UNREACHABLE();
        break;
    }
}

static bool unpackObject(JStarVM* vm, Obj* o, uint8_t n) {
    size_t size;
    Value* array;
    getValueArray(o, &array, &size);

    if(n > size) {
        jsrRaise(vm, "TypeException", "Too few values to unpack: expected %d, got %zu", n, size);
        return false;
    }

    for(int i = 0; i < n; i++) {
        push(vm, array[i]);
    }

    return true;
}

static bool unpackArgument(JStarVM* vm, Obj* o) {
    size_t size;
    Value* array;
    getValueArray(o, &array, &size);

    if(size >= UINT8_MAX) {
        jsrRaise(vm, "TypeException", "Argument too big to unpack: %zu", size);
        return false;
    }

    ensureStack(vm, size + 1);

    for(size_t i = 0; i < size; i++) {
        push(vm, array[i]);
    }

    push(vm, NUM_VAL(size));
    return true;
}

static JStarNative resolveNative(ObjModule* m, const char* cls, const char* name) {
    JStarNative n;
    if((n = resolveBuiltIn(m->name->data, cls, name)) != NULL) {
        return n;
    }

    JStarNativeReg* reg = m->natives.registry;
    if(reg != NULL) {
        for(int i = 0; reg[i].type != REG_SENTINEL; i++) {
            if(reg[i].type == REG_METHOD && cls != NULL) {
                const char* clsName = reg[i].as.method.cls;
                const char* methName = reg[i].as.method.name;
                if(strcmp(cls, clsName) == 0 && strcmp(name, methName) == 0) {
                    return reg[i].as.method.meth;
                }
            } else if(reg[i].type == REG_FUNCTION && cls == NULL) {
                const char* funName = reg[i].as.function.name;
                if(strcmp(name, funName) == 0) {
                    return reg[i].as.function.fun;
                }
            }
        }
    }

    return NULL;
}

// -----------------------------------------------------------------------------
// VM API
// -----------------------------------------------------------------------------

bool getFieldFromValue(JStarVM* vm, ObjString* name) {
    Value val = peek(vm);
    if(IS_OBJ(val)) {
        switch(OBJ_TYPE(val)) {
        case OBJ_INST: {
            Value v;
            ObjInstance* inst = AS_INSTANCE(val);
            if(!hashTableGet(&inst->fields, name, &v)) {
                // no field, try to bind method
                if(!bindMethod(vm, inst->base.cls, name)) {
                    jsrRaise(vm, "FieldException", "Object %s doesn't have field `%s`.",
                             inst->base.cls->name->data, name->data);
                    return false;
                }
                return true;
            }
            pop(vm);
            push(vm, v);
            return true;
        }
        case OBJ_MODULE: {
            Value v;
            ObjModule* mod = AS_MODULE(val);
            if(!hashTableGet(&mod->globals, name, &v)) {
                // if we didnt find a global name try to return bound method
                if(!bindMethod(vm, mod->base.cls, name)) {
                    jsrRaise(vm, "NameException", "Name `%s` is not defined in module %s",
                             name->data, mod->name->data);
                    return false;
                }
                return true;
            }
            pop(vm);
            push(vm, v);
            return true;
        }
        default:
            break;
        }
    }

    ObjClass* cls = getClass(vm, val);
    if(!bindMethod(vm, cls, name)) {
        jsrRaise(vm, "FieldException", "Object %s doesn't have field `%s`.", cls->name->data,
                 name->data);
        return false;
    }
    return true;
}

bool setFieldOfValue(JStarVM* vm, ObjString* name) {
    Value val = pop(vm);
    if(IS_OBJ(val)) {
        switch(OBJ_TYPE(val)) {
        case OBJ_INST: {
            ObjInstance* inst = AS_INSTANCE(val);
            hashTablePut(&inst->fields, name, peek(vm));
            return true;
        }
        case OBJ_MODULE: {
            ObjModule* mod = AS_MODULE(val);
            hashTablePut(&mod->globals, name, peek(vm));
            return true;
        }
        default:
            break;
        }
    }

    ObjClass* cls = getClass(vm, val);
    jsrRaise(vm, "FieldException", "Object %s doesn't have field `%s`.", cls->name->data,
             name->data);
    return false;
}

bool callValue(JStarVM* vm, Value callee, uint8_t argc) {
    if(IS_OBJ(callee)) {
        switch(OBJ_TYPE(callee)) {
        case OBJ_CLOSURE:
            return callFunction(vm, AS_CLOSURE(callee), argc);
        case OBJ_NATIVE:
            return callNative(vm, AS_NATIVE(callee), argc);
        case OBJ_BOUND_METHOD: {
            ObjBoundMethod* m = AS_BOUND_METHOD(callee);
            vm->sp[-argc - 1] = m->bound;
            if(m->method->type == OBJ_CLOSURE) {
                return callFunction(vm, (ObjClosure*)m->method, argc);
            } else {
                return callNative(vm, (ObjNative*)m->method, argc);
            }
        }
        case OBJ_CLASS: {
            ObjClass* cls = AS_CLASS(callee);

            if(isNonInstantiableBuiltin(vm, cls)) {
                jsrRaise(vm, "Exception", "class %s can't be directly instatiated",
                         cls->name->data);
                return false;
            }

            bool builtin = isInstatiableBuiltin(vm, cls);
            vm->sp[-argc - 1] = builtin ? NULL_VAL : OBJ_VAL(newInstance(vm, cls));

            Value ctor;
            if(hashTableGet(&cls->methods, vm->ctor, &ctor)) {
                return callValue(vm, ctor, argc);
            } else if(argc != 0) {
                jsrRaise(vm, "TypeException",
                         "Function %s.new() Expected 0 args, but instead `%d` supplied.",
                         cls->name->data, argc);
                return false;
            }

            return true;
        }
        default:
            break;
        }
    }

    ObjClass* cls = getClass(vm, callee);
    jsrRaise(vm, "TypeException", "Object %s is not a callable.", cls->name->data);
    return false;
}

bool invokeValue(JStarVM* vm, ObjString* name, uint8_t argc) {
    Value val = peekn(vm, argc);
    if(IS_OBJ(val)) {
        switch(OBJ_TYPE(val)) {
        case OBJ_INST: {
            Value f;
            ObjInstance* inst = AS_INSTANCE(val);

            // Check if field shadows a method
            if(hashTableGet(&inst->fields, name, &f)) {
                return callValue(vm, f, argc);
            }

            return invokeMethod(vm, inst->base.cls, name, argc);
        }
        case OBJ_MODULE: {
            Value func;
            ObjModule* mod = AS_MODULE(val);

            // check if method shadows a function in the module
            if(hashTableGet(&vm->modClass->methods, name, &func)) {
                return callValue(vm, func, argc);
            }

            if(!hashTableGet(&mod->globals, name, &func)) {
                jsrRaise(vm, "NameException", "Name `%s` is not defined in module %s.", name->data,
                         mod->name->data);
                return false;
            }

            return callValue(vm, func, argc);
        }
        default:
            break;
        }
    }

    ObjClass* cls = getClass(vm, val);
    return invokeMethod(vm, cls, name, argc);
}

inline void ensureStack(JStarVM* vm, size_t needed) {
    if(vm->sp + needed < vm->stack + vm->stackSz) return;

    Value* oldStack = vm->stack;
    vm->stackSz = powerOf2Ceil(vm->stackSz);
    vm->stack = realloc(vm->stack, sizeof(Value) * vm->stackSz);

    if(vm->stack != oldStack) {
        if(vm->apiStack >= vm->stack && vm->apiStack <= vm->sp) {
            vm->apiStack = vm->stack + (vm->apiStack - oldStack);
        }

        for(int i = 0; i < vm->frameCount; i++) {
            Frame* frame = &vm->frames[i];
            frame->stack = vm->stack + (frame->stack - oldStack);
            for(int j = 0; j < frame->handlerc; j++) {
                Handler* h = &frame->handlers[j];
                h->savesp = vm->stack + (h->savesp - oldStack);
            }
        }

        ObjUpvalue* upvalue = vm->upvalues;
        while(upvalue) {
            upvalue->addr = vm->stack + (upvalue->addr - oldStack);
            upvalue = upvalue->next;
        }

        vm->sp = vm->stack + (vm->sp - oldStack);
    }
}

// -----------------------------------------------------------------------------
// EVAL LOOP
// -----------------------------------------------------------------------------

// Enumeration encoding the cause of the stack unwinding,
// used during unwinding to correctly handle the execution
// of except/ensure handlers on return and exception
typedef enum UnwindCause {
    CAUSE_EXCEPT,
    CAUSE_RETURN,
} UnwindCause;

bool runEval(JStarVM* vm, int evalDepth) {
    register Frame* frame;
    register Value* frameStack;
    register ObjClosure* closure;
    register ObjFunction* fn;
    register uint8_t* ip;

    ASSERT(vm->frameCount != 0, "No frame to evaluate");
    ASSERT(vm->frameCount >= evalDepth, "Too few frame to evaluate");

#define LOAD_STATE()                             \
    do {                                         \
        frame = &vm->frames[vm->frameCount - 1]; \
        frameStack = frame->stack;               \
        closure = (ObjClosure*)frame->fn;        \
        fn = closure->fn;                        \
        ip = frame->ip;                          \
    } while(0)

#define SAVE_STATE() (frame->ip = ip)

#define NEXT_CODE()  (*ip++)
#define NEXT_SHORT() (ip += 2, ((uint16_t)ip[-2] << 8) | ip[-1])

#define GET_CONST()  (fn->code.consts.arr[NEXT_SHORT()])
#define GET_STRING() (AS_STRING(GET_CONST()))

#define BINARY(type, op, overload, reverse)         \
    do {                                            \
        if(IS_NUM(peek(vm)) && IS_NUM(peek2(vm))) { \
            double b = AS_NUM(pop(vm));             \
            double a = AS_NUM(pop(vm));             \
            push(vm, type(a op b));                 \
        } else {                                    \
            BINARY_OVERLOAD(op, overload, reverse); \
        }                                           \
    } while(0)

#define BINARY_OVERLOAD(op, overload, reverse)                     \
    do {                                                           \
        SAVE_STATE();                                              \
        bool res = callBinaryOverload(vm, #op, overload, reverse); \
        LOAD_STATE();                                              \
        if(!res) UNWIND_STACK(vm);                                 \
    } while(0)

#define RESTORE_HANDLER(h, frame, cause, excVal) \
    do {                                         \
        frame->ip = h->address;                  \
        vm->sp = h->savesp;                      \
        closeUpvalues(vm, vm->sp - 1);           \
        push(vm, excVal);                        \
        push(vm, NUM_VAL(cause));                \
    } while(0)

#define UNWIND_STACK(vm)                  \
    do {                                  \
        SAVE_STATE();                     \
        if(!unwindStack(vm, evalDepth)) { \
            return false;                 \
        }                                 \
        LOAD_STATE();                     \
        DISPATCH();                       \
    } while(0)

#define CHECK_EVAL_BREAK(vm)                        \
    do {                                            \
        if(vm->evalBreak) {                         \
            vm->evalBreak = 0;                      \
            jsrRaise(vm, "ProgramInterrupt", NULL); \
            UNWIND_STACK(vm);                       \
        }                                           \
    } while(0)

#ifdef JSTAR_DBG_PRINT_EXEC
    #define PRINT_DBG_STACK()                        \
        printf("     ");                             \
        for(Value* v = vm->stack; v < vm->sp; v++) { \
            printf("[");                             \
            printValue(*v);                          \
            printf("]");                             \
        }                                            \
        printf("$\n");                               \
        disassembleIstr(&fn->code, (size_t)(ip - fn->code.bytecode));
#else
    #define PRINT_DBG_STACK()
#endif

#ifdef JSTAR_COMPUTED_GOTOS
    // create jumptable
    static void* opJmpTable[] = {
    #define OPCODE(opcode, _) &&TARGET_##opcode,
    #include "opcode.def"
    };

    #define TARGET(op) TARGET_##op
    #define DISPATCH()                            \
        do {                                      \
            PRINT_DBG_STACK()                     \
            goto* opJmpTable[(op = NEXT_CODE())]; \
        } while(0)

    #define DECODE(op) DISPATCH();
#else
    #define TARGET(op) case op
    #define DISPATCH() goto decode
    #define DECODE(op)     \
    decode:                \
        PRINT_DBG_STACK(); \
        switch((op = NEXT_CODE()))
#endif

    // clang-format off

    LOAD_STATE();

    uint8_t op;
    DECODE(op) {

    TARGET(OP_ADD): {
        if(IS_NUM(peek(vm)) && IS_NUM(peek2(vm))) {
            double b = AS_NUM(pop(vm));
            double a = AS_NUM(pop(vm));
            push(vm, NUM_VAL(a + b));
        } else if(IS_STRING(peek(vm)) && IS_STRING(peek2(vm))) {
            ObjString* conc = stringConcatenate(vm, AS_STRING(peek2(vm)), AS_STRING(peek(vm)));
            pop(vm), pop(vm);
            push(vm, OBJ_VAL(conc));
        } else {
            BINARY_OVERLOAD(+, ADD_OVERLOAD, RADD_OVERLOAD);
        }
        DISPATCH();
    }

    TARGET(OP_SUB): {
        BINARY(NUM_VAL, -, SUB_OVERLOAD, RSUB_OVERLOAD);
        DISPATCH();
    }

    TARGET(OP_MUL): {
        BINARY(NUM_VAL, *, MUL_OVERLOAD, RMUL_OVERLOAD);
        DISPATCH();
    }

    TARGET(OP_DIV): {
        BINARY(NUM_VAL, /, DIV_OVERLOAD, RDIV_OVERLOAD);
        DISPATCH();
    }
    
    TARGET(OP_MOD): {
        if(IS_NUM(peek(vm)) && IS_NUM(peek2(vm))) {
            double b = AS_NUM(pop(vm));
            double a = AS_NUM(pop(vm));
            push(vm, NUM_VAL(fmod(a, b)));
        } else {
            BINARY_OVERLOAD(%, MOD_OVERLOAD, RMOD_OVERLOAD);
        }
        DISPATCH();
    }
    
    TARGET(OP_POW): {
        if(!IS_NUM(peek(vm)) || !IS_NUM(peek2(vm))) {
            jsrRaise(vm, "TypeException", "Operands of `^` must be numbers");
            UNWIND_STACK(vm);
        }
        double y = AS_NUM(pop(vm));
        double x = AS_NUM(pop(vm));
        push(vm, NUM_VAL(pow(x, y)));
        DISPATCH();
    }

    TARGET(OP_NEG): {
        if(IS_NUM(peek(vm))) {
            push(vm, NUM_VAL(-AS_NUM(pop(vm))));
        } else {
            ObjClass* cls = getClass(vm, peek(vm));
            SAVE_STATE();
            bool res = invokeMethod(vm, cls, vm->overloads[NEG_OVERLOAD], 0);
            LOAD_STATE();
            if(!res) UNWIND_STACK(vm);
        }
        DISPATCH();
    }

    TARGET(OP_LT): {
        BINARY(BOOL_VAL, <, LT_OVERLOAD, OVERLOAD_SENTINEL);
        DISPATCH();
    }

    TARGET(OP_LE): {
        BINARY(BOOL_VAL, <=, LE_OVERLOAD, OVERLOAD_SENTINEL);
        DISPATCH();
    }

    TARGET(OP_GT): {
        BINARY(BOOL_VAL, >, GT_OVERLOAD, OVERLOAD_SENTINEL);
        DISPATCH();
    }

    TARGET(OP_GE): {
        BINARY(BOOL_VAL, >=, GE_OVERLOAD, OVERLOAD_SENTINEL);
        DISPATCH();
    }

    TARGET(OP_EQ): {
        if(IS_NUM(peek2(vm)) || IS_NULL(peek2(vm)) || IS_BOOL(peek2(vm))) {
            push(vm, BOOL_VAL(valueEquals(pop(vm), pop(vm))));
        } else {
            BINARY_OVERLOAD(==, EQ_OVERLOAD, OVERLOAD_SENTINEL);
        }
        DISPATCH();
    }

    TARGET(OP_NOT): {
        push(vm, BOOL_VAL(!valueToBool(pop(vm))));
        DISPATCH();
    }

    TARGET(OP_IS): {
        if(!IS_CLASS(peek(vm))) {
            jsrRaise(vm, "TypeException", "Right operand of `is` must be a class.");
            UNWIND_STACK(vm);
        }
        Value b = pop(vm);
        Value a = pop(vm);
        push(vm, BOOL_VAL(isInstance(vm, a, AS_CLASS(b))));
        DISPATCH();
    }

    TARGET(OP_SUBSCR_GET): {
        SAVE_STATE();
        bool res = getSubscriptOfValue(vm);
        LOAD_STATE();
        if(!res) UNWIND_STACK(vm);
        DISPATCH();
    }

    TARGET(OP_SUBSCR_SET): {
        SAVE_STATE();
        bool res = setSubscriptOfValue(vm);
        LOAD_STATE();
        if(!res) UNWIND_STACK(vm);
        DISPATCH();
    }

    TARGET(OP_GET_FIELD): {
        if(!getFieldFromValue(vm, GET_STRING())) {
            UNWIND_STACK(vm);
        }
        DISPATCH();
    }

    TARGET(OP_SET_FIELD): {
        if(!setFieldOfValue(vm, GET_STRING())) {
            UNWIND_STACK(vm);
        }
        DISPATCH();
    }

    TARGET(OP_JUMP): {
        int16_t off = NEXT_SHORT();
        ip += off;
        CHECK_EVAL_BREAK(vm);
        DISPATCH();
    }

    TARGET(OP_JUMPF): {
        int16_t off = NEXT_SHORT();
        if(!valueToBool(pop(vm))) ip += off;
        DISPATCH();
    }

    TARGET(OP_JUMPT): {
        int16_t off = NEXT_SHORT();
        if(valueToBool(pop(vm))) ip += off;
        DISPATCH();
    }

    TARGET(OP_FOR_ITER): {
        vm->sp[0] = vm->sp[-2];
        vm->sp[1] = vm->sp[-1];
        vm->sp += 2;
        SAVE_STATE();
        bool res = invokeValue(vm, vm->iter, 1);
        LOAD_STATE();
        if(!res) UNWIND_STACK(vm);
        DISPATCH();
    }

    TARGET(OP_FOR_NEXT): {
        vm->sp[-2] = vm->sp[-1];
        int16_t off = NEXT_SHORT();
        if(valueToBool(pop(vm))) {
            vm->sp[0] = vm->sp[-2];
            vm->sp[1] = vm->sp[-1];
            vm->sp += 2;
            SAVE_STATE();
            bool res = invokeValue(vm, vm->next, 1);
            LOAD_STATE();
            if(!res) UNWIND_STACK(vm);
        } else {
            ip += off;
        }
        DISPATCH();
    }

    TARGET(OP_NULL): {
        push(vm, NULL_VAL);
        DISPATCH();
    }

    {
        uint8_t argc;

    TARGET(OP_CALL_0):
    TARGET(OP_CALL_1):
    TARGET(OP_CALL_2):
    TARGET(OP_CALL_3):
    TARGET(OP_CALL_4):
    TARGET(OP_CALL_5):
    TARGET(OP_CALL_6):
    TARGET(OP_CALL_7):
    TARGET(OP_CALL_8):
    TARGET(OP_CALL_9):
    TARGET(OP_CALL_10):
        argc = op - OP_CALL_0;
        goto call;

    TARGET(OP_CALL_UNPACK):
        if(!computeUnpackCallArgc(vm, AS_NUM(pop(vm)), NEXT_CODE(), &argc)) {
            UNWIND_STACK(vm);
        }
        goto call;

    TARGET(OP_CALL):
        argc = NEXT_CODE();

call:
        SAVE_STATE();
        bool res = callValue(vm, peekn(vm, argc), argc);
        LOAD_STATE();
        if(!res) UNWIND_STACK(vm);
        DISPATCH();
    }

    {
        uint8_t argc;

    TARGET(OP_INVOKE_0):
    TARGET(OP_INVOKE_1):
    TARGET(OP_INVOKE_2):
    TARGET(OP_INVOKE_3):
    TARGET(OP_INVOKE_4):
    TARGET(OP_INVOKE_5):
    TARGET(OP_INVOKE_6):
    TARGET(OP_INVOKE_7):
    TARGET(OP_INVOKE_8):
    TARGET(OP_INVOKE_9):
    TARGET(OP_INVOKE_10):
        argc = op - OP_INVOKE_0;
        goto invoke;

    TARGET(OP_INVOKE_UNPACK): {
        if(!computeUnpackCallArgc(vm, AS_NUM(pop(vm)), NEXT_CODE(), &argc)) {
            UNWIND_STACK(vm);
        }
        goto invoke;
    }
    
    TARGET(OP_INVOKE):
        argc = NEXT_CODE();

invoke:;
        ObjString* name = GET_STRING();
        SAVE_STATE();
        bool res = invokeValue(vm, name, argc);
        LOAD_STATE();
        if(!res) UNWIND_STACK(vm);
        DISPATCH();
    }
    
    {
        uint8_t argc;

    TARGET(OP_SUPER_0):
    TARGET(OP_SUPER_1):
    TARGET(OP_SUPER_2):
    TARGET(OP_SUPER_3):
    TARGET(OP_SUPER_4):
    TARGET(OP_SUPER_5):
    TARGET(OP_SUPER_6):
    TARGET(OP_SUPER_7):
    TARGET(OP_SUPER_8):
    TARGET(OP_SUPER_9):
    TARGET(OP_SUPER_10):
        argc = op - OP_SUPER_0;
        goto supinvoke;

    TARGET(OP_SUPER_UNPACK): {
        if(!computeUnpackCallArgc(vm, AS_NUM(pop(vm)), NEXT_CODE(), &argc)) {
            UNWIND_STACK(vm);
        }
        goto supinvoke;
    }

    TARGET(OP_SUPER):
        argc = NEXT_CODE();

supinvoke:;
        ObjString* name = GET_STRING();
        // The superclass is stored as a const in the function itself
        ObjClass* sup = AS_CLASS(fn->code.consts.arr[0]);
        SAVE_STATE();
        bool res = invokeMethod(vm, sup, name, argc);
        LOAD_STATE();
        if(!res) UNWIND_STACK(vm);
        DISPATCH();
    }

    TARGET(OP_SUPER_BIND): {
        ObjString* name = GET_STRING();
        // The superclass is stored as a const in the function itself
        ObjClass* cls = AS_CLASS(fn->code.consts.arr[0]);
        if(!bindMethod(vm, cls, name)) {
            jsrRaise(vm, "MethodException", "Method %s.%s() doesn't exists", cls->name->data,
                     name->data);
            UNWIND_STACK(vm);
        }
        DISPATCH();
    }

op_return:
    TARGET(OP_RETURN): {
        Value ret = pop(vm);
        CHECK_EVAL_BREAK(vm);

        while(frame->handlerc > 0) {
            Handler* h = &frame->handlers[--frame->handlerc];
            if(h->type == HANDLER_ENSURE) {
                RESTORE_HANDLER(h, frame, CAUSE_RETURN, ret);
                LOAD_STATE();
                DISPATCH();
            }
        }

        closeUpvalues(vm, frameStack);
        vm->sp = frameStack;
        push(vm, ret);

        if(--vm->frameCount == evalDepth) {
            return true;
        }

        LOAD_STATE();
        vm->module = fn->c.module;

        DISPATCH();
    }

    TARGET(OP_IMPORT): 
    TARGET(OP_IMPORT_AS):
    TARGET(OP_IMPORT_FROM): {
        ObjString* name = GET_STRING();
        if(!importModule(vm, name)) {
            jsrRaise(vm, "ImportException", "Cannot load module `%s`.", name->data);
            UNWIND_STACK(vm);
        }

        switch(op) {
        case OP_IMPORT:
            hashTablePut(&vm->module->globals, name, OBJ_VAL(getModule(vm, name)));
            break;
        case OP_IMPORT_AS:
            hashTablePut(&vm->module->globals, GET_STRING(), OBJ_VAL(getModule(vm, name)));
            break;
        }

        //call the module's main if first time import
        if(!valueEquals(peek(vm), NULL_VAL)) {
            SAVE_STATE();
            callFunction(vm, AS_CLOSURE(peek(vm)), 0);
            LOAD_STATE();
        }
        DISPATCH();
    }
    
    TARGET(OP_IMPORT_NAME): {
        ObjModule* m = getModule(vm, GET_STRING());
        ObjString* n = GET_STRING();

        if(n->data[0] == '*') {
            hashTableImportNames(&vm->module->globals, &m->globals);
        } else {
            Value val;
            if(!hashTableGet(&m->globals, n, &val)) {
                jsrRaise(vm, "NameException", "Name `%s` not defined in module `%s`.", 
                         n->data, m->name->data);
                UNWIND_STACK(vm);
            } 
            hashTablePut(&vm->module->globals, n, val);
        }
        DISPATCH();
    }

    TARGET(OP_NEW_LIST): {
        push(vm, OBJ_VAL(newList(vm, 0)));
        DISPATCH();
    }

    TARGET(OP_APPEND_LIST): {
        listAppend(vm, AS_LIST(peek2(vm)), peek(vm));
        pop(vm);
        DISPATCH();
    }
    
    TARGET(OP_NEW_TUPLE): {
        uint8_t size = NEXT_CODE();
        ObjTuple* t = newTuple(vm, size);
        for(int i = size - 1; i >= 0; i--) {
            t->arr[i] = pop(vm);
        }
        push(vm, OBJ_VAL(t));
        DISPATCH();
    }

    TARGET(OP_NEW_TABLE): {
        push(vm, OBJ_VAL(newTable(vm)));
        DISPATCH();
    }

    TARGET(OP_CLOSURE): {
        ObjClosure* c = newClosure(vm, AS_FUNC(GET_CONST()));
        push(vm, OBJ_VAL(c));
        for(uint8_t i = 0; i < c->upvalueCount; i++) {
            uint8_t isLocal = NEXT_CODE();
            uint8_t index = NEXT_CODE();
            if(isLocal) {
                c->upvalues[i] = captureUpvalue(vm, frame->stack + index);
            } else {
                c->upvalues[i] = ((ObjClosure*)frame->fn)->upvalues[index];
            }
        }
        DISPATCH();
    }

    TARGET(OP_NEW_CLASS): {
        createClass(vm, GET_STRING(), vm->objClass);
        DISPATCH();
    }

    TARGET(OP_NEW_SUBCLASS): {
        if(!IS_CLASS(peek(vm))) {
            jsrRaise(vm, "TypeException", "Superclass in class declaration must be a Class.");
            UNWIND_STACK(vm);
        }
        ObjClass* cls = AS_CLASS(pop(vm));
        if(isBuiltinClass(vm, cls)) {
            jsrRaise(vm, "TypeException", "Cannot subclass builtin class %s", cls->name->data);
            UNWIND_STACK(vm);
        }
        createClass(vm, GET_STRING(), cls);
        DISPATCH();
    }

    TARGET(OP_UNPACK): {
        if(!IS_LIST(peek(vm)) && !IS_TUPLE(peek(vm))) {
            jsrRaise(vm, "TypeException", "Can unpack only Tuple or List, got %s.",
                     getClass(vm, peek(vm))->name->data);
            UNWIND_STACK(vm);
        }
        if(!unpackObject(vm, AS_OBJ(pop(vm)),  NEXT_CODE())) {
            UNWIND_STACK(vm);
        }
        DISPATCH();
    }

    TARGET(OP_UNPACK_ARG): {
        if(!IS_LIST(peek(vm)) && !IS_TUPLE(peek(vm))) {
            jsrRaise(vm, "TypeException", "Can unpack only Tuple or List, got %s.",
                     getClass(vm, peek(vm))->name->data);
            UNWIND_STACK(vm);
        }
        if(!unpackArgument(vm, AS_OBJ(pop(vm)))) {
            UNWIND_STACK(vm);
        }
        DISPATCH();
    }
    
    TARGET(OP_DEF_METHOD): {
        ObjClass* cls = AS_CLASS(peek2(vm));
        ObjString* methodName = GET_STRING();
        // Set the superclass as a const in the function
        AS_CLOSURE(peek(vm))->fn->code.consts.arr[0] = OBJ_VAL(cls->superCls);
        hashTablePut(&cls->methods, methodName, pop(vm));
        DISPATCH();
    }
    
    TARGET(OP_NAT_METHOD): {
        ObjClass* cls = AS_CLASS(peek(vm));
        ObjString* methodName = GET_STRING();
        ObjNative* native = AS_NATIVE(GET_CONST());
        native->fn = resolveNative(vm->module, cls->name->data, methodName->data);
        if(native->fn == NULL) {
            jsrRaise(vm, "Exception", "Cannot resolve native method %s().", native->c.name->data);
            UNWIND_STACK(vm);
        }
        hashTablePut(&cls->methods, methodName, OBJ_VAL(native));
        DISPATCH();
    }

    TARGET(OP_NATIVE): {
        ObjString* name = GET_STRING();
        ObjNative* nat  = AS_NATIVE(peek(vm));
        nat->fn = resolveNative(vm->module, NULL, name->data);
        if(nat->fn == NULL) {
            jsrRaise(vm, "Exception", "Cannot resolve native function %s.%s.", 
                     vm->module->name->data, nat->c.name->data);
            UNWIND_STACK(vm);
        }
        DISPATCH();
    }
    
    TARGET(OP_GET_CONST): {
        push(vm, GET_CONST());
        DISPATCH();
    }

    TARGET(OP_DEFINE_GLOBAL): {
        hashTablePut(&vm->module->globals, GET_STRING(), pop(vm));
        DISPATCH();
    }

    TARGET(OP_GET_GLOBAL): {
        ObjString* name = GET_STRING();
        if(!hashTableGet(&vm->module->globals, name, vm->sp)) {
            if(!hashTableGet(&vm->core->globals, name, vm->sp)) {
                jsrRaise(vm, "NameException", "Name `%s` is not defined.", name->data);
                UNWIND_STACK(vm);
            }
        }
        vm->sp++;
        DISPATCH();
    }

    TARGET(OP_SET_GLOBAL): {
        ObjString* name = GET_STRING();
        if(hashTablePut(&vm->module->globals, name, peek(vm))) {
            jsrRaise(vm, "NameException", "Name `%s` is not defined.", name->data);
            UNWIND_STACK(vm);
        }
        DISPATCH();
    }

    TARGET(OP_SETUP_EXCEPT): 
    TARGET(OP_SETUP_ENSURE): {
        uint16_t offset = NEXT_SHORT();
        Handler* handler = &frame->handlers[frame->handlerc++];
        handler->address = ip + offset;
        handler->savesp = vm->sp;
        handler->type = op;
        DISPATCH();
    }
    
    TARGET(OP_END_TRY): {
        if(!IS_NULL(peek2(vm))) {
            UnwindCause cause = AS_NUM(pop(vm));
            switch(cause) {
            case CAUSE_EXCEPT:
                // continue unwinding
                UNWIND_STACK(vm);
                break;
            case CAUSE_RETURN:
                // return will handle ensure handlers
                goto op_return;
            default:
                UNREACHABLE();
                break;
            }
        }
        DISPATCH();
    }

    TARGET(OP_POP_HANDLER): {
        frame->handlerc--;
        DISPATCH();
    }
    
    TARGET(OP_RAISE): {
        Value exc = peek(vm);
        if(!isInstance(vm, exc, vm->excClass)) {
            jsrRaise(vm, "TypeException", "Can only raise Exception instances.");
            UNWIND_STACK(vm);
        }
        ObjStackTrace* st = newStackTrace(vm);
        push(vm, OBJ_VAL(st));
        ObjInstance* excInst = AS_INSTANCE(exc);
        hashTablePut(&excInst->fields, copyString(vm, EXC_TRACE, strlen(EXC_TRACE)), OBJ_VAL(st));
        pop(vm);
        UNWIND_STACK(vm);
    }

    TARGET(OP_GET_LOCAL): {
        push(vm, frameStack[NEXT_CODE()]);
        DISPATCH();
    }

    TARGET(OP_SET_LOCAL): {
        frameStack[NEXT_CODE()] = peek(vm);
        DISPATCH();
    }

    TARGET(OP_GET_UPVALUE): {
        push(vm, *closure->upvalues[NEXT_CODE()]->addr);
        DISPATCH();
    }

    TARGET(OP_SET_UPVALUE): {
        *closure->upvalues[NEXT_CODE()]->addr = peek(vm);
        DISPATCH();
    }

    TARGET(OP_POP): {
        pop(vm);
        DISPATCH();
    }

    TARGET(OP_CLOSE_UPVALUE): {
        closeUpvalues(vm, vm->sp - 1);
        pop(vm);
        DISPATCH();
    }

    TARGET(OP_DUP): {
        *vm->sp = *(vm->sp - 1);
        vm->sp++;
        DISPATCH();
    }

    TARGET(OP_SIGN_CONT):
    TARGET(OP_SIGN_BRK):
        UNREACHABLE();
        return false;

    }

    // clang-format on

    UNREACHABLE();
    return false;
}

bool unwindStack(JStarVM* vm, int depth) {
    ASSERT(isInstance(vm, peek(vm), vm->excClass), "Top of stack is not an Exception");
    ObjInstance* exception = AS_INSTANCE(peek(vm));

    Value stacktraceVal = NULL_VAL;
    hashTableGet(&exception->fields, copyString(vm, EXC_TRACE, strlen(EXC_TRACE)), &stacktraceVal);
    ASSERT(IS_STACK_TRACE(stacktraceVal), "Exception doesn't have a stacktrace object");
    ObjStackTrace* stackTrace = AS_STACK_TRACE(stacktraceVal);

    for(; vm->frameCount > depth; vm->frameCount--) {
        Frame* frame = &vm->frames[vm->frameCount - 1];

        switch(frame->fn->type) {
        case OBJ_CLOSURE:
            vm->module = ((ObjClosure*)frame->fn)->fn->c.module;
            break;
        case OBJ_NATIVE:
            vm->module = ((ObjNative*)frame->fn)->c.module;
            break;
        default:
            UNREACHABLE();
            break;
        }

        stRecordFrame(vm, stackTrace, frame, vm->frameCount);

        // if current frame has except or ensure handlers restore handler state and exit
        if(frame->handlerc > 0) {
            Value exc = pop(vm);
            Handler* h = &frame->handlers[--frame->handlerc];
            RESTORE_HANDLER(h, frame, CAUSE_EXCEPT, exc);
            return true;
        }

        closeUpvalues(vm, frame->stack);
    }

    // we have reached the end of the stack or a native/function boundary,
    // return from evaluation leaving the exception on top of the stack
    return false;
}

// Inline function declarations
extern inline void push(JStarVM* vm, Value v);
extern inline Value pop(JStarVM* vm);
extern inline Value peek(JStarVM* vm);
extern inline Value peek2(JStarVM* vm);
extern inline Value peekn(JStarVM* vm, int n);
extern inline ObjClass* getClass(JStarVM* vm, Value v);
extern inline bool isInstance(JStarVM* vm, Value i, ObjClass* cls);
extern inline int apiStackIndex(JStarVM* vm, int slot);
extern inline Value apiStackSlot(JStarVM* vm, int slot);