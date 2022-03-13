#include "vm.h"

#include <math.h>
#include <string.h>

#include "builtins/builtins.h"
#include "builtins/core.h"
#include "code.h"
#include "disassemble.h"
#include "gc.h"
#include "import.h"
#include "opcode.h"
#include "profiler.h"

static const char* const methodSyms[SYM_END] = {
    [SYM_CTOR] = CTOR_STR,        [SYM_ITER] = "__iter__",      [SYM_NEXT] = "__next__",
    [SYM_ADD] = "__add__",        [SYM_SUB] = "__sub__",        [SYM_MUL] = "__mul__",
    [SYM_DIV] = "__div__",        [SYM_MOD] = "__mod__",        [SYM_BAND] = "__band__",
    [SYM_BOR] = "__bor__",        [SYM_XOR] = "__xor__",        [SYM_LSHFT] = "__lshift__",
    [SYM_RSHFT] = "__rshift__",   [SYM_RADD] = "__radd__",      [SYM_RSUB] = "__rsub__",
    [SYM_RMUL] = "__rmul__",      [SYM_RDIV] = "__rdiv__",      [SYM_RMOD] = "__rmod__",
    [SYM_RBAND] = "__rband__",    [SYM_RBOR] = "__rbor__",      [SYM_RXOR] = "__rxor__",
    [SYM_RLSHFT] = "__rlshift__", [SYM_RRSHFT] = "__rrshift__", [SYM_GET] = "__get__",
    [SYM_SET] = "__set__",        [SYM_EQ] = "__eq__",          [SYM_LT] = "__lt__",
    [SYM_LE] = "__le__",          [SYM_GT] = "__gt__",          [SYM_GE] = "__ge__",
    [SYM_NEG] = "__neg__",        [SYM_INV] = "__invert__",     [SYM_POW] = "__pow__",
    [SYM_RPOW] = "__rpow__",
};

// Prepare an exception or ensure handler in the vm for execution
#define RESTORE_HANDLER(vm, h, frame, cause, exc) \
    do {                                          \
        frame->ip = h->address;                   \
        vm->sp = h->savedSp;                      \
        closeUpvalues(vm, vm->sp);                \
        push(vm, exc);                            \
        push(vm, NUM_VAL(cause));                 \
    } while(0)

// Enumeration encoding the cause of stack unwinding.
// Used during unwinding to correctly handle the execution
// of except/ensure handlers on return and exception
typedef enum UnwindCause {
    CAUSE_EXCEPT,
    CAUSE_RETURN,
} UnwindCause;

// Enumeration encoding the action to be taken upon generator reusme.
// WARNING: This enumeration is synchronized to GenSend, GenThrow and 
// GenClose variables in core.jsr
typedef enum GenAction {
    GEN_SEND,
    GEN_THROW,
    GEN_CLOSE
} GenAction;

// -----------------------------------------------------------------------------
// VM INITIALIZATION AND DESTRUCTION
// -----------------------------------------------------------------------------

static void resetStack(JStarVM* vm) {
    vm->sp = vm->stack;
    vm->apiStack = vm->stack;
    vm->frameCount = 0;
    vm->module = NULL;
}

static size_t roundUp(size_t num, size_t multiple) {
    return ((num + multiple - 1) / multiple) * multiple;
}

JStarVM* jsrNewVM(const JStarConf* conf) {
    PROFILE_FUNC()

    JStarVM* vm = calloc(1, sizeof(*vm));
    vm->errorCallback = conf->errorCallback;
    vm->customData = conf->customData;

    // VM program stack
    vm->stackSz = roundUp(conf->startingStackSize, MAX_LOCALS + 1);
    vm->frameSz = vm->stackSz / (MAX_LOCALS + 1);
    vm->stack = malloc(sizeof(Value) * vm->stackSz);
    vm->frames = malloc(sizeof(Frame) * vm->frameSz);
    resetStack(vm);

    // GC Values
    vm->nextGC = conf->firstGCCollectionPoint;
    vm->heapGrowRate = conf->heapGrowRate;

    // Module cache and interned string pool
    initHashTable(&vm->modules);
    initHashTable(&vm->stringPool);

    // Create string constants of special method names
    for(int i = 0; i < SYM_END; i++) {
        vm->methodSyms[i] = copyString(vm, methodSyms[i], strlen(methodSyms[i]));
    }

    // Core module bootstrap
    initCoreModule(vm);

    // Create empty tuple singleton
    vm->emptyTup = newTuple(vm, 0);

    return vm;
}

void jsrFreeVM(JStarVM* vm) {
    PROFILE_FUNC()

    resetStack(vm);

    {
        PROFILE("{free-vm-state}::jsrFreeVM")

        free(vm->stack);
        free(vm->frames);
        freeHashTable(&vm->stringPool);
        freeHashTable(&vm->modules);
    }

    sweepObjects(vm);

#ifdef JSTAR_DBG_PRINT_GC
    printf("Allocated at exit: %lu bytes.\n", vm->allocated);
#endif

    free(vm);
}

// -----------------------------------------------------------------------------
// VM IMPLEMENTATION
// -----------------------------------------------------------------------------

static Frame* getFrame(JStarVM* vm) {
    if(vm->frameCount + 1 == vm->frameSz) {
        vm->frameSz *= 2;
        vm->frames = realloc(vm->frames, sizeof(Frame) * vm->frameSz);
    }
    return &vm->frames[vm->frameCount++];
}

static Frame* initFrame(JStarVM* vm, Prototype* proto) {
    Frame* callFrame = getFrame(vm);
    callFrame->stack = vm->sp - (proto->argsCount + 1) - (int)proto->vararg;
    callFrame->handlerCount = 0;
    return callFrame;
}

static Frame* appendCallFrame(JStarVM* vm, ObjClosure* closure) {
    Frame* callFrame = initFrame(vm, &closure->fn->proto);
    callFrame->gen = NULL;
    callFrame->fn = (Obj*)closure;
    callFrame->ip = closure->fn->code.bytecode;
    return callFrame;
}

static Frame* appendNativeFrame(JStarVM* vm, ObjNative* native) {
    Frame* callFrame = initFrame(vm, &native->proto);
    callFrame->fn = (Obj*)native;
    callFrame->ip = NULL;
    return callFrame;
}

static bool isNonInstantiableBuiltin(JStarVM* vm, ObjClass* cls) {
    return cls == vm->nullClass || cls == vm->funClass || cls == vm->modClass ||
           cls == vm->stClass || cls == vm->clsClass || cls == vm->udataClass ||
           cls == vm->genClass;
}

static bool isInstatiableBuiltin(JStarVM* vm, ObjClass* cls) {
    return cls == vm->lstClass || cls == vm->tupClass || cls == vm->numClass ||
           cls == vm->boolClass || cls == vm->strClass || cls == vm->tableClass;
}

static bool isBuiltinClass(JStarVM* vm, ObjClass* cls) {
    return isNonInstantiableBuiltin(vm, cls) || isInstatiableBuiltin(vm, cls);
}

static bool isInt(double n) {
    return trunc(n) == n;
}

static void createClass(JStarVM* vm, ObjString* name, ObjClass* superCls) {
    ObjClass* cls = newClass(vm, name, superCls);
    hashTableMerge(&cls->methods, &superCls->methods);
    push(vm, OBJ_VAL(cls));
}

static ObjUpvalue* captureUpvalue(JStarVM* vm, Value* addr) {
    if(!vm->upvalues) {
        vm->upvalues = newUpvalue(vm, addr);
        return vm->upvalues;
    }

    ObjUpvalue* prev = NULL;
    ObjUpvalue* upvalue = vm->upvalues;

    while(upvalue && upvalue->addr > addr) {
        prev = upvalue;
        upvalue = upvalue->next;
    }

    if(upvalue && upvalue->addr == addr) {
        return upvalue;
    }

    ObjUpvalue* created = newUpvalue(vm, addr);
    if(!prev) {
        vm->upvalues = created;
    } else {
        prev->next = created;
    }

    created->next = upvalue;
    return created;
}

static void closeUpvalues(JStarVM* vm, Value* last) {
    while(vm->upvalues && vm->upvalues->addr >= last) {
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

static void argumentError(JStarVM* vm, Prototype* proto, int expected, int supplied,
                          const char* quantity) {
    jsrRaise(vm, "TypeException", "Function `%s.%s` takes %s %d arguments, %d supplied.",
             proto->module->name->data, proto->name->data, quantity, expected, supplied);
}

static bool adjustArguments(JStarVM* vm, Prototype* p, uint8_t argc) {
    uint8_t most = p->argsCount, least = most - p->defCount;

    if(!p->vararg && argc > most) {
        argumentError(vm, p, p->argsCount, argc, most == least ? "exactly" : "at most");
        return false;
    } else if(argc < least) {
        argumentError(vm, p, least, argc, (most == least && !p->vararg) ? "exactly" : "at least");
        return false;
    }

    // Push remaining args taking the default value
    for(uint8_t i = argc - least; i < p->defCount; i++) {
        push(vm, p->defaults[i]);
    }

    if(p->vararg) {
        packVarargs(vm, argc > most ? argc - most : 0);
    }

    return true;
}

static bool checkStackOverflow(JStarVM* vm) {
    if(vm->frameCount + 1 >= MAX_FRAMES) {
        jsrRaise(vm, "StackOverflowException", "Exceeded maximum recursion depth");
        return false;
    }
    return true;
}

static bool callFunction(JStarVM* vm, ObjClosure* closure, uint8_t argc) {
    if(!checkStackOverflow(vm)) {
        return false;
    }

    if(!adjustArguments(vm, &closure->fn->proto, argc)) {
        return false;
    }

    reserveStack(vm, closure->fn->stackUsage);
    appendCallFrame(vm, closure);
    vm->module = closure->fn->proto.module;

    return true;
}

static bool callNative(JStarVM* vm, ObjNative* native, uint8_t argc) {
    if(!checkStackOverflow(vm)) {
        return false;
    }

    if(!adjustArguments(vm, &native->proto, argc)) {
        return false;
    }

    reserveStack(vm, JSTAR_MIN_NATIVE_STACK_SZ);
    Frame* frame = appendNativeFrame(vm, native);

    ObjModule* oldModule = vm->module;
    size_t savedApiStack = vm->apiStack - vm->stack;

    vm->module = native->proto.module;
    vm->apiStack = frame->stack;

    if(!native->fn(vm)) {
        vm->module = oldModule;
        vm->apiStack = vm->stack + savedApiStack;
        return false;
    }

    Value ret = pop(vm);
    vm->frameCount--;
    vm->sp = vm->apiStack;
    vm->module = oldModule;
    vm->apiStack = vm->stack + savedApiStack;

    push(vm, ret);
    return true;
}

static void saveFrame(ObjGenerator* gen, uint8_t* ip, Value* sp, const Frame* f) {
    PROFILE_FUNC()

    size_t stackTop = (size_t)(ptrdiff_t)(sp - f->stack);
    ASSERT(stackTop <= gen->stackSize, "Insufficient generator stak size");

    gen->frame.ip = ip;
    gen->frame.stackTop = stackTop;
    gen->frame.handlerCount = f->handlerCount;

    // Save function stack
    memcpy(gen->savedStack, f->stack, stackTop * sizeof(Value));

    // Save exception handlers
    for(int i = 0; i < f->handlerCount; i++) {
        const Handler* handler = &f->handlers[i];
        SavedHandler* saved = &gen->frame.handlers[i];
        saved->type = handler->type;
        saved->address = handler->address;
        saved->spOffset = (size_t)(ptrdiff_t)(handler->savedSp - f->stack);
    }
}

static Value* restoreFrame(ObjGenerator* gen, Value* sp, Frame* f) {
    PROFILE_FUNC()

    f->gen = gen;
    f->fn = (Obj*)gen->closure;
    f->ip = gen->frame.ip;
    f->stack = sp;
    f->handlerCount = gen->frame.handlerCount;

    // Restore function stack
    memcpy(f->stack, gen->savedStack, gen->frame.stackTop * sizeof(Value));

    // Restore exception handlers
    for(int i = 0; i < f->handlerCount; i++) {
        Handler* handler = &f->handlers[i];
        const SavedHandler* savedHandler = &gen->frame.handlers[i];
        handler->type = savedHandler->type;
        handler->address = savedHandler->address;
        handler->savedSp = sp + savedHandler->spOffset;
    }

    // New stack pointer
    return f->stack + gen->frame.stackTop;
}

static bool resumeGenerator(JStarVM* vm, ObjGenerator* gen, uint8_t argc) {
    PROFILE_FUNC()

    if(gen->state == GEN_DONE) {
        jsrRaise(vm, "Exception", "Generator has completed"); // TODO: launch another exception
        return false;
    }

    if(gen->state == GEN_RUNNING) {
        jsrRaise(vm, "Exception", "Generator already running"); // TODO: launch another exception
        return false;
    }

    bool inCoreModule = vm->module == vm->core;
    if(!inCoreModule && argc > 1) {
        jsrRaise(vm, "TypeException", "Generator takes at most 1 argument, %d supplied", (int)argc);
        return false;
    }

    GenAction action = GEN_SEND;
    if(inCoreModule) {
        ASSERT(IS_NUM(peek(vm)), "Action is not an integer");
        action = AS_NUM(pop(vm));
    }

    Value value = NULL_VAL;
    if(argc) {
        value = pop(vm);
    }

    Frame *frame = getFrame(vm);
    reserveStack(vm, gen->frame.stackTop);
    vm->sp = restoreFrame(gen, vm->sp - 1, frame);
    vm->module = gen->closure->fn->proto.module;

    if(!checkStackOverflow(vm)) {
        return false;
    }

    switch(action) {
    case GEN_SEND:
        gen->state = GEN_RUNNING;
        if(gen->state == GEN_SUSPENDED) {
            push(vm, value);
        }
        return true;
    case GEN_THROW:
        push(vm, value);
        jsrRaiseException(vm, -1);
        return false;
    case GEN_CLOSE:
        gen->state = GEN_DONE;

        while(frame->handlerCount > 0) {
            Handler* h = &frame->handlers[--frame->handlerCount];
            if(h->type == HANDLER_ENSURE) {
                RESTORE_HANDLER(vm, h, frame, CAUSE_RETURN, value);
                return true;
            }
        }

        push(vm, value);
        return true;
    }

    UNREACHABLE();
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
    if(slice->size != 2) {
        jsrRaise(vm, "TypeException", "Slice index must have two elements.");
        return false;
    }

    if(!IS_INT(slice->arr[0]) || !IS_INT(slice->arr[1])) {
        jsrRaise(vm, "TypeException", "Slice index must be two integers.");
        return false;
    }

    size_t a = jsrCheckIndexNum(vm, AS_NUM(slice->arr[0]), size + 1);
    if(a == SIZE_MAX) return false;
    size_t b = jsrCheckIndexNum(vm, AS_NUM(slice->arr[1]), size + 1);
    if(b == SIZE_MAX) return false;

    if(a > b) {
        jsrRaise(vm, "InvalidArgException",
                 "Invalid slice indices (%g, %g), first must be <= than second",
                 AS_NUM(slice->arr[0]), AS_NUM(slice->arr[1]));
        return false;
    }

    *low = a, *high = b;
    return true;
}

static bool getListSubscript(JStarVM* vm) {
    ObjList* lst = AS_LIST(peek2(vm));
    Value arg = peek(vm);

    if(IS_INT(arg)) {
        size_t idx = jsrCheckIndexNum(vm, AS_NUM(arg), lst->size);
        if(idx == SIZE_MAX) return false;

        pop(vm), pop(vm);
        push(vm, lst->arr[idx]);
        return true;
    }
    if(IS_TUPLE(arg)) {
        size_t low = 0, high = 0;
        if(!checkSliceIndex(vm, AS_TUPLE(arg), lst->size, &low, &high)) return false;

        ObjList* ret = newList(vm, high - low);
        ret->size = high - low;
        for(size_t i = low; i < high; i++) {
            ret->arr[i - low] = lst->arr[i];
        }

        pop(vm), pop(vm);
        push(vm, OBJ_VAL(ret));
        return true;
    }

    jsrRaise(vm, "TypeException", "Index of List subscript must be an integer or a Tuple");
    return false;
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

    jsrRaise(vm, "TypeException", "Index of Tuple subscript must be an integer or a Tuple");
    return false;
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

    jsrRaise(vm, "TypeException", "Index of String subscript must be an integer or a Tuple");
    return false;
}

static void concatStrings(JStarVM* vm) {
    ObjString *s1 = AS_STRING(peek2(vm)), *s2 = AS_STRING(peek(vm));
    size_t length = s1->length + s2->length;
    ObjString* conc = allocateString(vm, length);
    memcpy(conc->data, s1->data, s1->length);
    memcpy(conc->data + s1->length, s2->data, s2->length);
    pop(vm), pop(vm);
    push(vm, OBJ_VAL(conc));
}

static bool binOverload(JStarVM* vm, const char* op, MethodSymbol overload, MethodSymbol reverse) {
    Value method;
    ObjClass* cls1 = getClass(vm, peek2(vm));

    if(hashTableGet(&cls1->methods, vm->methodSyms[overload], &method)) {
        return callValue(vm, method, 1);
    }

    ObjClass* cls2 = getClass(vm, peek(vm));
    if(reverse != SYM_END) {
        swapStackSlots(vm, -1, -2);

        if(hashTableGet(&cls2->methods, vm->methodSyms[reverse], &method)) {
            return callValue(vm, method, 1);
        }
    }

    jsrRaise(vm, "TypeException", "Operator %s not defined for types %s, %s", op, cls1->name->data,
             cls2->name->data);
    return false;
}

static bool unaryOverload(JStarVM* vm, const char* op, MethodSymbol overload) {
    Value method;
    ObjClass* cls = getClass(vm, peek(vm));

    if(hashTableGet(&cls->methods, vm->methodSyms[overload], &method)) {
        return callValue(vm, method, 0);
    }

    jsrRaise(vm, "TypeException", "Unary operator %s not defined for type %s", op, cls->name->data);
    return false;
}

static bool unpackArgs(JStarVM* vm, uint8_t argc, uint8_t* out) {
    if(argc == 0) {
        jsrRaise(vm, "TypeException", "No argument to unpack");
        return false;
    }

    if(!IS_LIST(peek(vm)) && !IS_TUPLE(peek(vm))) {
        jsrRaise(vm, "TypeException", "Can unpack only Tuple or List, got %s.",
                 getClass(vm, peek(vm))->name->data);
        return false;
    }

    size_t size;
    Value* array = getValues(AS_OBJ(pop(vm)), &size);

    size_t totalArgc = argc + size - 1;
    if(totalArgc >= UINT8_MAX) {
        jsrRaise(vm, "TypeException", "Too many arguments for function call: %zu", totalArgc);
        return false;
    }

    *out = (uint8_t)totalArgc;

    reserveStack(vm, size + 1);
    for(size_t i = 0; i < size; i++) {
        push(vm, array[i]);
    }

    return true;
}

static bool unpackObject(JStarVM* vm, Obj* o, uint8_t n) {
    size_t size;
    Value* array = getValues(o, &size);

    if(n > size) {
        jsrRaise(vm, "TypeException", "Too few values to unpack: expected %d, got %zu", n, size);
        return false;
    }

    for(int i = 0; i < n; i++) {
        push(vm, array[i]);
    }

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

bool getValueField(JStarVM* vm, ObjString* name) {
    Value val = peek(vm);
    if(IS_OBJ(val)) {
        switch(AS_OBJ(val)->type) {
        case OBJ_INST: {
            Value field;
            ObjInstance* inst = AS_INSTANCE(val);

            // Try top find a field
            if(!hashTableGet(&inst->fields, name, &field)) {
                // No field, try to bind method
                if(!bindMethod(vm, inst->base.cls, name)) {
                    jsrRaise(vm, "FieldException", "Object %s doesn't have field `%s`.",
                             inst->base.cls->name->data, name->data);
                    return false;
                }
                return true;
            }

            pop(vm);
            push(vm, field);
            return true;
        }
        case OBJ_MODULE: {
            Value global;
            ObjModule* mod = AS_MODULE(val);

            // Try to find global variable
            if(!hashTableGet(&mod->globals, name, &global)) {
                // No global, try to bind method
                if(!bindMethod(vm, mod->base.cls, name)) {
                    jsrRaise(vm, "NameException", "Name `%s` is not defined in module %s",
                             name->data, mod->name->data);
                    return false;
                }
                return true;
            }

            pop(vm);
            push(vm, global);
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

bool setValueField(JStarVM* vm, ObjString* name) {
    Value val = pop(vm);
    if(IS_OBJ(val)) {
        switch(AS_OBJ(val)->type) {
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

bool getValueSubscript(JStarVM* vm) {
    if(IS_OBJ(peek2(vm))) {
        Value operand = peek2(vm);
        switch(AS_OBJ(operand)->type) {
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

    if(!invokeMethod(vm, getClass(vm, peek2(vm)), vm->methodSyms[SYM_GET], 1)) {
        return false;
    }
    return true;
}

bool setValueSubscript(JStarVM* vm) {
    if(IS_LIST(peek(vm))) {
        Value operand = pop(vm), arg = pop(vm), val = peek(vm);

        if(!IS_NUM(arg) || !isInt(AS_NUM(arg))) {
            jsrRaise(vm, "TypeException", "Index of List subscript access must be an integer.");
            return false;
        }

        ObjList* list = AS_LIST(operand);
        size_t index = jsrCheckIndexNum(vm, AS_NUM(arg), list->size);
        if(index == SIZE_MAX) return false;

        list->arr[index] = val;
        return true;
    }

    // Swap operand and value to prepare function call
    swapStackSlots(vm, -1, -3);
    if(!invokeMethod(vm, getClass(vm, peekn(vm, 2)), vm->methodSyms[SYM_SET], 2)) {
        return false;
    }
    return true;
}

bool callValue(JStarVM* vm, Value callee, uint8_t argc) {
    if(IS_OBJ(callee)) {
        switch(AS_OBJ(callee)->type) {
        case OBJ_CLOSURE:
            return callFunction(vm, AS_CLOSURE(callee), argc);
        case OBJ_NATIVE:
            return callNative(vm, AS_NATIVE(callee), argc);
        case OBJ_GENERATOR:
            return resumeGenerator(vm, AS_GENERATOR(callee), argc);
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

            if(isInstatiableBuiltin(vm, cls)) {
                vm->sp[-argc - 1] = NULL_VAL;
            } else {
                vm->sp[-argc - 1] = OBJ_VAL(newInstance(vm, cls));
            }

            Value ctor;
            if(hashTableGet(&cls->methods, vm->methodSyms[SYM_CTOR], &ctor)) {
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
        switch(AS_OBJ(val)->type) {
        case OBJ_INST: {
            ObjInstance* inst = AS_INSTANCE(val);
            ObjClass* cls = inst->base.cls;

            // First try to find a method
            Value method;
            if(hashTableGet(&cls->methods, name, &method)) {
                return callValue(vm, method, argc);
            }

            // If no method is found try a field
            Value field;
            if(hashTableGet(&inst->fields, name, &field)) {
                return callValue(vm, field, argc);
            }

            jsrRaise(vm, "MethodException", "Method %s.%s() doesn't exists", cls->name->data,
                     name->data);
            return false;
        }
        case OBJ_MODULE: {
            Value func;
            ObjModule* mod = AS_MODULE(val);

            // Check if method shadows a function in the module
            if(hashTableGet(&vm->modClass->methods, name, &func)) {
                return callValue(vm, func, argc);
            }

            // If no method is found on the ObjModule, try to get global variable
            if(hashTableGet(&mod->globals, name, &func)) {
                return callValue(vm, func, argc);
            }

            jsrRaise(vm, "NameException", "Name `%s` is not defined in module %s.", name->data,
                     mod->name->data);
            return false;
        }
        default:
            break;
        }
    }

    ObjClass* cls = getClass(vm, val);
    return invokeMethod(vm, cls, name, argc);
}

static int powerOf2Ceil(int n) {
    n--;
    n |= n >> 1;
    n |= n >> 2;
    n |= n >> 4;
    n |= n >> 8;
    n |= n >> 16;
    n++;
    return n;
}

inline void reserveStack(JStarVM* vm, size_t needed) {
    if(vm->sp + needed < vm->stack + vm->stackSz) return;

    PROFILE_FUNC()

    Value* oldStack = vm->stack;
    vm->stackSz = powerOf2Ceil(vm->stackSz + needed);
    vm->stack = realloc(vm->stack, sizeof(Value) * vm->stackSz);

    if(vm->stack != oldStack) {
        PROFILE("{restore-stack}::reserveStack")

        if(vm->apiStack >= oldStack && vm->apiStack <= vm->sp) {
            vm->apiStack = vm->stack + (vm->apiStack - oldStack);
        }

        for(int i = 0; i < vm->frameCount; i++) {
            Frame* frame = &vm->frames[i];
            frame->stack = vm->stack + (frame->stack - oldStack);
            for(int j = 0; j < frame->handlerCount; j++) {
                Handler* h = &frame->handlers[j];
                h->savedSp = vm->stack + (h->savedSp - oldStack);
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

inline void swapStackSlots(JStarVM* vm, int a, int b) {
    Value tmp = vm->sp[a];
    vm->sp[a] = vm->sp[b];
    vm->sp[b] = tmp;
}

// -----------------------------------------------------------------------------
// EVAL LOOP
// -----------------------------------------------------------------------------

bool runEval(JStarVM* vm, int evalDepth) {
    PROFILE_FUNC()

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
        DISPATCH();                                 \
    } while(0)

#define BINARY_OVERLOAD(op, overload, reverse)              \
    do {                                                    \
        SAVE_STATE();                                       \
        bool res = binOverload(vm, #op, overload, reverse); \
        LOAD_STATE();                                       \
        if(!res) UNWIND_STACK(vm);                          \
    } while(0)

#define BITWISE(name, op, overload, reverse)                                           \
    do {                                                                               \
        if(IS_NUM(peek(vm)) && IS_NUM(peek2(vm))) {                                    \
            double b = AS_NUM(pop(vm));                                                \
            double a = AS_NUM(pop(vm));                                                \
            if(!HAS_INT_REPR(a) || !HAS_INT_REPR(b)) {                                 \
                jsrRaise(vm, "TypeException", "Number has no integer representation"); \
                UNWIND_STACK(vm);                                                      \
            }                                                                          \
            push(vm, NUM_VAL((int64_t)a op (int64_t)b));                               \
        } else {                                                                       \
            BINARY_OVERLOAD(name, overload, reverse);                                  \
        }                                                                              \
        DISPATCH();                                                                    \
    } while(0)

#define UNARY(type, op, overload)               \
    do {                                        \
        if(IS_NUM(peek(vm))) {                  \
            double n = AS_NUM(pop(vm));         \
            push(vm, type(op(n)));              \
        } else {                                \
            UNARY_OVERLOAD(type, op, overload); \
        }                                       \
        DISPATCH();                             \
    } while(0)

#define UNARY_OVERLOAD(type, op, overload)           \
    do {                                             \
        SAVE_STATE();                                \
        bool res = unaryOverload(vm, #op, overload); \
        LOAD_STATE();                                \
        if(!res) UNWIND_STACK(vm);                   \
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
        disassembleInstr(&fn->code, 0, (size_t)(ip - fn->code.bytecode));
#else
    #define PRINT_DBG_STACK()
#endif

#ifdef JSTAR_COMPUTED_GOTOS
    // create jumptable
    static void* opJmpTable[] = {
    #define OPCODE(opcode, args, stack) &&TARGET_##opcode,
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
            concatStrings(vm);
        } else {
            BINARY_OVERLOAD(+, SYM_ADD, SYM_RADD);
        }
        DISPATCH();
    }
    
    TARGET(OP_MOD): {
        if(IS_NUM(peek(vm)) && IS_NUM(peek2(vm))) {
            double b = AS_NUM(pop(vm));
            double a = AS_NUM(pop(vm));
            push(vm, NUM_VAL(fmod(a, b)));
        } else {
            BINARY_OVERLOAD(%, SYM_MOD, SYM_RMOD);
        }
        DISPATCH();
    }
            
    TARGET(OP_POW): {
        if(IS_NUM(peek(vm)) && IS_NUM(peek2(vm))) {
            double y = AS_NUM(pop(vm));
            double x = AS_NUM(pop(vm));
            push(vm, NUM_VAL(pow(x, y)));
        } else {
            BINARY_OVERLOAD(^, SYM_POW, SYM_RPOW);
        }
        DISPATCH();
    }

    TARGET(OP_EQ): {
        if(IS_NUM(peek2(vm)) || IS_NULL(peek2(vm)) || IS_BOOL(peek2(vm))) {
            push(vm, BOOL_VAL(valueEquals(pop(vm), pop(vm))));
        } else {
            BINARY_OVERLOAD(==, SYM_EQ, SYM_END);
        }
        DISPATCH();
    }

    TARGET(OP_INVERT): {
        if(IS_NUM(peek(vm))) {
            double x = AS_NUM(pop(vm));
            if(!HAS_INT_REPR(x)) {
                jsrRaise(vm, "TypeException", "Number has no integer representation");
                UNWIND_STACK(vm);
            }
            push(vm, NUM_VAL(~(int64_t)x));
        } else {
            UNARY_OVERLOAD(NUM_VAL, ~, SYM_INV);
        }
        DISPATCH();
    }

    TARGET(OP_NOT): {
        push(vm, BOOL_VAL(!valueToBool(pop(vm))));
        DISPATCH();
    }

    TARGET(OP_SUB):    BINARY(NUM_VAL, -, SYM_SUB, SYM_RSUB);
    TARGET(OP_MUL):    BINARY(NUM_VAL, *, SYM_MUL, SYM_RMUL);
    TARGET(OP_DIV):    BINARY(NUM_VAL, /, SYM_DIV, SYM_RDIV);
    TARGET(OP_LT):     BINARY(BOOL_VAL, <, SYM_LT, SYM_END);
    TARGET(OP_LE):     BINARY(BOOL_VAL, <=, SYM_LE, SYM_END);
    TARGET(OP_GT):     BINARY(BOOL_VAL, >, SYM_GT, SYM_END);
    TARGET(OP_GE):     BINARY(BOOL_VAL, >=, SYM_GE, SYM_END);
    TARGET(OP_LSHIFT): BITWISE(<<, <<, SYM_LSHFT, SYM_RLSHFT);
    TARGET(OP_RSHIFT): BITWISE(>>, >>, SYM_RSHFT, SYM_RRSHFT);
    TARGET(OP_BAND):   BITWISE(&, &, SYM_BAND, SYM_RBAND);
    TARGET(OP_BOR):    BITWISE(|, |, SYM_BOR, SYM_RBOR);
    TARGET(OP_XOR):    BITWISE(~, ^, SYM_XOR, SYM_RXOR);
    TARGET(OP_NEG):    UNARY(NUM_VAL, -, SYM_NEG);

    TARGET(OP_IS): {
        if(!IS_CLASS(peek(vm))) {
            jsrRaise(vm, "TypeException", "Right operand of `is` must be a Class");
            UNWIND_STACK(vm);
        }
        Value b = pop(vm);
        Value a = pop(vm);
        push(vm, BOOL_VAL(isInstance(vm, a, AS_CLASS(b))));
        DISPATCH();
    }

    TARGET(OP_SUBSCR_GET): {
        SAVE_STATE();
        bool res = getValueSubscript(vm);
        LOAD_STATE();
        if(!res) UNWIND_STACK(vm);
        DISPATCH();
    }

    TARGET(OP_SUBSCR_SET): {
        SAVE_STATE();
        bool res = setValueSubscript(vm);
        LOAD_STATE();
        if(!res) UNWIND_STACK(vm);
        DISPATCH();
    }

    TARGET(OP_GET_FIELD): {
        if(!getValueField(vm, GET_STRING())) {
            UNWIND_STACK(vm);
        }
        DISPATCH();
    }

    TARGET(OP_SET_FIELD): {
        if(!setValueField(vm, GET_STRING())) {
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

    TARGET(OP_FOR_PREP): {
        ObjClass* cls = getClass(vm, vm->sp[-2]);
        if(!hashTableGet(&cls->methods, vm->methodSyms[SYM_ITER], &vm->sp[0]) ||
           !hashTableGet(&cls->methods, vm->methodSyms[SYM_NEXT], &vm->sp[1])) {
            jsrRaise(vm, "MethodException", "Class %s does not implement __iter__ and __next__",
                     cls->name->data);
            UNWIND_STACK(vm);
        }
        vm->sp += 2;
        DISPATCH();
    }

    TARGET(OP_FOR_ITER): {
        vm->sp[0] = vm->sp[-4];
        vm->sp[1] = vm->sp[-3];
        vm->sp += 2;
        SAVE_STATE();
        bool res = callValue(vm, vm->sp[-4], 1); // sp[-4] holds the cached __iter__ method
        LOAD_STATE();
        if(!res) UNWIND_STACK(vm);
        DISPATCH();
    }

    TARGET(OP_FOR_NEXT): {
        int16_t off = NEXT_SHORT();
        vm->sp[-4] = vm->sp[-1];
        if(valueToBool(pop(vm))) {
            vm->sp[0] = vm->sp[-4];
            vm->sp[1] = vm->sp[-3];
            vm->sp += 2;
            SAVE_STATE();
            bool res = callValue(vm, vm->sp[-3], 1); // sp[-3] holds the cached __next__ method
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
        if(!unpackArgs(vm, NEXT_CODE(), &argc)) {
            UNWIND_STACK(vm);
        }
        goto call;

    TARGET(OP_CALL):
        argc = NEXT_CODE();
        goto call;

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

    TARGET(OP_INVOKE_UNPACK):
        if(!unpackArgs(vm, NEXT_CODE(), &argc)) {
            UNWIND_STACK(vm);
        }
        goto invoke;

    TARGET(OP_INVOKE):
        argc = NEXT_CODE();
        goto invoke;

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

    TARGET(OP_SUPER_UNPACK):
        if(!unpackArgs(vm, NEXT_CODE(), &argc)) {
            UNWIND_STACK(vm);
        }
        goto supinvoke;

    TARGET(OP_SUPER):
        argc = NEXT_CODE();
        goto supinvoke;

supinvoke:;
        ObjString* name = GET_STRING();
        ObjClass* superCls = AS_CLASS(fn->code.consts.arr[SUPER_SLOT]);
        SAVE_STATE();
        bool res = invokeMethod(vm, superCls, name, argc);
        LOAD_STATE();
        if(!res) UNWIND_STACK(vm);
        DISPATCH();
    }

    TARGET(OP_SUPER_BIND): {
        ObjString* name = GET_STRING();
        ObjClass* superCls = AS_CLASS(fn->code.consts.arr[SUPER_SLOT]);
        if(!bindMethod(vm, superCls, name)) {
            jsrRaise(vm, "MethodException", "Method %s.%s() doesn't exists", superCls->name->data,
                     name->data);
            UNWIND_STACK(vm);
        }
        DISPATCH();
    }

op_return:
    TARGET(OP_RETURN): {
        Value ret = pop(vm);
        CHECK_EVAL_BREAK(vm);

        while(frame->handlerCount > 0) {
            Handler* h = &frame->handlers[--frame->handlerCount];
            if(h->type == HANDLER_ENSURE) {
                RESTORE_HANDLER(vm, h, frame, CAUSE_RETURN, ret);
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
        vm->module = fn->proto.module;

        DISPATCH();
    }

    TARGET(OP_YIELD): {
        ASSERT(frame->gen, "Current function is not a Generator");
        Value ret = pop(vm);
        // TODO: check eval break?

        ObjGenerator* gen = frame->gen;
        saveFrame(frame->gen, ip, vm->sp, frame);
        gen->state = GEN_SUSPENDED;
        gen->lastYield = ret;

        closeUpvalues(vm, frameStack);
        vm->sp = frameStack;
        push(vm, ret);

        if(--vm->frameCount == evalDepth) {
            return true;
        }

        LOAD_STATE();
        vm->module = fn->proto.module;

        DISPATCH();
    }

    TARGET(OP_IMPORT): 
    TARGET(OP_IMPORT_FROM): {
        ObjString* name = GET_STRING();
        ObjModule* module = importModule(vm, name);

        if(module == NULL) {
            jsrRaise(vm, "ImportException", "Cannot load module `%s`.", name->data);
            UNWIND_STACK(vm);
        }

        if(op == OP_IMPORT) {
            push(vm, OBJ_VAL(module));
            swapStackSlots(vm, -1, -2);
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
        ObjModule* module = getModule(vm, GET_STRING());
        ObjString* name = GET_STRING();
        if(!hashTableGet(&module->globals, name, vm->sp)) {
            jsrRaise(vm, "NameException", "Name `%s` not defined in module `%s`.", 
                     name->data, module->name->data);
            UNWIND_STACK(vm);
        }
        vm->sp++;
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

    TARGET(OP_GENERATOR): {
        const Prototype* p = &fn->proto;
        ObjGenerator* gen = newGenerator(vm, closure, fn->stackUsage + p->argsCount + p->vararg);
        saveFrame(gen, ip, vm->sp, frame);
        push(vm, OBJ_VAL(gen));
        goto op_return;
    }

    TARGET(OP_GENERATOR_CLOSE): {
        ASSERT(frame->gen, "Current function isn't a Generator");
        frame->gen->state = GEN_DONE;
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
        if(!unpackObject(vm, AS_OBJ(pop(vm)), NEXT_CODE())) {
            UNWIND_STACK(vm);
        }
        DISPATCH();
    }
    
    TARGET(OP_DEF_METHOD): {
        ObjClass* cls = AS_CLASS(peek2(vm));
        ObjString* methodName = GET_STRING();
        // Set the super-class as a const in the method
        AS_CLOSURE(peek(vm))->fn->code.consts.arr[SUPER_SLOT] = OBJ_VAL(cls->superCls);
        hashTablePut(&cls->methods, methodName, pop(vm));
        DISPATCH();
    }
    
    TARGET(OP_NAT_METHOD): {
        ObjClass* cls = AS_CLASS(peek(vm));
        ObjString* methodName = GET_STRING();
        ObjNative* native = AS_NATIVE(GET_CONST());
        native->fn = resolveNative(vm->module, cls->name->data, methodName->data);
        if(native->fn == NULL) {
            jsrRaise(vm, "Exception", "Cannot resolve native method %s().", native->proto.name->data);
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
                     vm->module->name->data, nat->proto.name->data);
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
            jsrRaise(vm, "NameException", "Name `%s` is not defined.", name->data);
            UNWIND_STACK(vm);
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
        Handler* handler = &frame->handlers[frame->handlerCount++];
        handler->type = op == OP_SETUP_ENSURE ? HANDLER_ENSURE : HANDLER_EXCEPT;
        handler->address = ip + offset;
        handler->savedSp = vm->sp;
        DISPATCH();
    }
    
    TARGET(OP_END_HANDLER): {
        if(!IS_NULL(peek(vm))) { // Is the exception still unhandled?
            UnwindCause cause = AS_NUM(pop(vm));
            switch(cause) {
            case CAUSE_EXCEPT:
                // Continue unwinding
                UNWIND_STACK(vm); 
                break;
            case CAUSE_RETURN:
                // OP_RETURN will execute ensure handlers
                goto op_return;
            default:
                UNREACHABLE();
                break;
            }
        }
        DISPATCH();
    }

    TARGET(OP_POP_HANDLER): {
        frame->handlerCount--;
        DISPATCH();
    }
    
    TARGET(OP_RAISE): {
        jsrRaiseException(vm, -1);
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

    TARGET(OP_POPN): {
        uint8_t n = NEXT_CODE();
        vm->sp -= n;
        closeUpvalues(vm, vm->sp);
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

    TARGET(OP_END): {
        UNREACHABLE();
    }

    }

    // clang-format on

    UNREACHABLE();
    return false;
}

bool unwindStack(JStarVM* vm, int depth) {
    PROFILE_FUNC()

    ASSERT(isInstance(vm, peek(vm), vm->excClass), "Top of stack is not an Exception");
    ObjInstance* exception = AS_INSTANCE(peek(vm));

    Value stacktraceVal = NULL_VAL;
    hashTableGet(&exception->fields, copyString(vm, EXC_TRACE, strlen(EXC_TRACE)), &stacktraceVal);
    ASSERT(IS_STACK_TRACE(stacktraceVal), "Exception doesn't have a stacktrace object");
    ObjStackTrace* stacktrace = AS_STACK_TRACE(stacktraceVal);

    for(; vm->frameCount > depth; vm->frameCount--) {
        Frame* frame = &vm->frames[vm->frameCount - 1];

        switch(frame->fn->type) {
        case OBJ_CLOSURE: {
            ObjClosure* closure = (ObjClosure*)frame->fn;
            vm->module = closure->fn->proto.module;
            break;
        }
        case OBJ_NATIVE: {
            ObjNative* native = (ObjNative*)frame->fn;
            vm->module = native->proto.module;
            break;
        }
        default:
            UNREACHABLE();
            break;
        }

        stacktraceDump(vm, stacktrace, frame, vm->frameCount);

        // If current frame has except or ensure handlers restore handler state and exit
        if(frame->handlerCount > 0) {
            Value exc = pop(vm);
            Handler* h = &frame->handlers[--frame->handlerCount];
            RESTORE_HANDLER(vm, h, frame, CAUSE_EXCEPT, exc);
            return true;
        }

        // If this a generator function set it as completed
        if(frame->gen) {
            frame->gen->state = GEN_DONE;
        }

        closeUpvalues(vm, frame->stack);
    }

    // We have reached the end of the stack or a native/function boundary,
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