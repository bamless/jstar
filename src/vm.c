#include "vm.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "array.h"
#include "builtins/builtins.h"
#include "builtins/core/core.h"
#include "builtins/core/excs.h"
#include "conf.h"
#include "gc.h"
#include "import.h"
#include "jstar.h"
#include "object.h"
#include "opcode.h"
#include "parse/ast.h"
#include "profiler.h"
#include "symbol.h"
#include "util.h"
#include "value.h"
#include "value_hashtable.h"
#if defined(JSTAR_DBG_PRINT_EXEC)
    #include "disassemble.h"
#endif

// Constant method names used in operator overloading
static const char* const specialMethods[METH_SIZE] = {
#define SPECIAL_METHOD(_, name) name,
#include "special_methods.def"
};

// Enumeration encoding the cause of stack unwinding.
// Used during unwinding to correctly handle the execution
// of except/ensure handlers on return and exception
typedef enum UnwindCause {
    CAUSE_EXCEPT,
    CAUSE_RETURN,
} UnwindCause;

// Enumeration encoding the action to be taken upon generator reusme.
// WARNING: This enumeration is synchronized to GEN_SEND, GEN_THROW
// and GEN_CLOSE variables in core.jsr
typedef enum GenAction {
    GEN_SEND,
    GEN_THROW,
    GEN_CLOSE,
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

    JStarRealloc reallocate = conf->realloc ? conf->realloc : defaultRealloc;
    JStarVM* vm = reallocate(NULL, 0, sizeof(*vm));
    JSR_ASSERT(vm, "Out of memory");
    memset(vm, 0, sizeof(*vm));

    vm->realloc = reallocate;
    vm->astArena.realloc = reallocate;
    vm->errorCallback = conf->errorCallback;
    vm->importCallback = conf->importCallback;
    vm->userData = conf->userData;

    // VM program stack
    vm->stackSz = roundUp(conf->startingStackSize, MAX_LOCALS + 1);
    vm->frameSz = vm->stackSz / (MAX_LOCALS + 1);

    vm->stack = vm->realloc(NULL, 0, sizeof(Value) * vm->stackSz);
    JSR_ASSERT(vm->stack, "Out of memory");

    vm->frames = vm->realloc(NULL, 0, sizeof(Frame) * vm->frameSz);
    JSR_ASSERT(vm->frames, "Out of memory");

    resetStack(vm);

    // GC Values
    vm->nextGC = conf->firstGCCollectionPoint;
    vm->heapGrowRate = conf->heapGrowRate;

    // Module cache and interned string pool
    initValueHashTable(vm, &vm->modules);
    initValueHashTable(vm, &vm->stringPool);

    // Create string constants of special method names
    for(int i = 0; i < METH_SIZE; i++) {
        vm->specialMethods[i] = copyString(vm, specialMethods[i], strlen(specialMethods[i]));
    }

    return vm;
}

void jsrInitRuntime(JStarVM* vm) {
    // Core module bootstrap
    initCoreModule(vm);

    // Initialize main module
    ObjString* name = copyString(vm, JSR_MAIN_MODULE, strlen(JSR_MAIN_MODULE));
    push(vm, OBJ_VAL(name));
    setModule(vm, name, newModule(vm, JSR_MAIN_MODULE, name));
    pop(vm);

    // Create singletons
    vm->emptyTup = newTuple(vm, 0);
    vm->excErr = copyString(vm, EXC_ERR, strlen(EXC_ERR));
    vm->excTrace = copyString(vm, EXC_TRACE, strlen(EXC_TRACE));
    vm->excCause = copyString(vm, EXC_CAUSE, strlen(EXC_CAUSE));
}

void jsrFreeVM(JStarVM* vm) {
    PROFILE_FUNC()

    resetStack(vm);

    {
        PROFILE("{free-vm-state}::jsrFreeVM")

        vm->realloc(vm->stack, vm->stackSz, 0);
        vm->realloc(vm->frames, vm->frameSz, 0);
        freeValueHashTable(&vm->stringPool);
        freeValueHashTable(&vm->modules);

        JStarSymbol* sym = vm->symbols;
        while(sym) {
            JStarSymbol* next = sym->next;
            GC_FREE(vm, JStarSymbol, sym);
            sym = next;
        }

        arrayFree(vm, &vm->reachedStack);
    }

    jsrASTArenaFree(&vm->astArena);
    sweepObjects(vm);

#ifdef JSTAR_DBG_PRINT_GC
    printf("Allocated at exit: %lu bytes.\n", vm->allocated);
#endif

    vm->realloc(vm, sizeof(*vm), 0);
}

// -----------------------------------------------------------------------------
// VM IMPLEMENTATION
// -----------------------------------------------------------------------------

static Frame* getFrame(JStarVM* vm) {
    if(vm->frameCount + 1 == vm->frameSz) {
        size_t oldSz = vm->frameSz;
        vm->frameSz *= 2;
        vm->frames = vm->realloc(vm->frames, oldSz * sizeof(Frame), vm->frameSz * sizeof(Frame));
        JSR_ASSERT(vm->frames, "Out of memory");
    }
    return &vm->frames[vm->frameCount++];
}

static Frame* initFrame(JStarVM* vm, Prototype* proto) {
    Frame* callFrame = getFrame(vm);
    callFrame->stack = vm->sp - (proto->argsCount + 1) - (int)proto->vararg;
    callFrame->handlerCount = 0;
    callFrame->gen = NULL;
    return callFrame;
}

static Frame* appendCallFrame(JStarVM* vm, ObjClosure* closure) {
    Frame* callFrame = initFrame(vm, &closure->fn->proto);
    callFrame->fn = (Obj*)closure;
    callFrame->ip = closure->fn->code.bytecode.items;
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

static bool isSymbolCached(JStarVM* vm, Obj* key, const SymbolCache* sym) {
    bool hit = sym->key == key;
#ifdef JSTAR_DBG_CACHE_STATS
    if(hit) vm->cacheHits++;
    else vm->cacheMisses++;
#endif
    return hit;
}

static void createClass(JStarVM* vm, ObjString* name) {
    push(vm, OBJ_VAL(newClass(vm, name, NULL)));
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

// Prepare an except or ensure handler for execution in the VM
static void restoreHandler(JStarVM* vm, Frame* f, const Handler* h, UnwindCause cause, Value val) {
    f->ip = h->address;
    vm->sp = h->savedSp;
    closeUpvalues(vm, vm->sp);
    // The exception/result and unwinding cause must be on top of the stack
    push(vm, val);
    push(vm, NUM_VAL(cause));
}

// Unwinds all handlers of the current frame, restoring the state of HANDLER_ENSUREs if encountered
static bool unwindHandlers(JStarVM* vm, Frame* frame, Value retVal) {
    while(frame->handlerCount > 0) {
        Handler* h = &frame->handlers[--frame->handlerCount];
        if(h->type == HANDLER_ENSURE) {
            restoreHandler(vm, frame, h, CAUSE_RETURN, retVal);
            return true;
        }
    }
    return false;
}

static void packVarargs(JStarVM* vm, uint8_t count) {
    ObjTuple* args = newTuple(vm, count);
    for(int i = count - 1; i >= 0; i--) {
        args->items[i] = pop(vm);
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

    reserveStack(vm, MAX_LOCALS);
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
    const Frame* frame = appendNativeFrame(vm, native);

    ObjModule* oldModule = vm->module;

    // Save the current apiStack position relative to the base of the stack. This is saved
    // in a relative way so that the apiStack can be restored to the correct position after
    // a possible stack reallocation (for example, if the native function or any of its nested
    // calls allocate more stack space than its currently available)
    size_t apiStackOffset = vm->apiStack - vm->stack;

    vm->module = native->proto.module;
    vm->apiStack = frame->stack;

    if(!native->fn(vm)) {
        vm->module = oldModule;
        vm->apiStack = vm->stack + apiStackOffset;
        return false;
    }

    Value ret = pop(vm);
    vm->frameCount--;
    vm->sp = vm->apiStack;
    push(vm, ret);

    vm->module = oldModule;
    vm->apiStack = vm->stack + apiStackOffset;
    return true;
}

static void saveFrame(ObjGenerator* gen, uint8_t* ip, Value* sp, const Frame* f) {
    PROFILE_FUNC()

    size_t stackTop = (size_t)(sp - f->stack);
    JSR_ASSERT(stackTop <= gen->stackSize, "Insufficient generator stack size");

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
        saved->spOffset = (size_t)(handler->savedSp - f->stack);
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
        const SavedHandler* savedHandler = &gen->frame.handlers[i];
        Handler* handler = &f->handlers[i];
        handler->type = savedHandler->type;
        handler->address = savedHandler->address;
        handler->savedSp = sp + savedHandler->spOffset;
    }

    // New stack pointer
    return f->stack + gen->frame.stackTop;
}

static bool resumeGenerator(JStarVM* vm, ObjGenerator* gen, uint8_t argc) {
    PROFILE_FUNC()

    if(gen->state == GEN_DONE || gen->state == GEN_RUNNING) {
        jsrRaise(vm, "GeneratorException",
                 gen->state == GEN_DONE ? "Generator has completed"
                                        : "Generator is already running");
        return false;
    }

    bool inCoreModule = vm->module == vm->core;
    if(!inCoreModule && argc > 1) {
        jsrRaise(vm, "TypeException", "Generator takes at most 1 argument, %d supplied", (int)argc);
        return false;
    }

    GenAction action = GEN_SEND;
    if(inCoreModule && argc) {
        JSR_ASSERT(IS_NUM(peek(vm)), "Action is not an integer");
        action = AS_NUM(pop(vm));
    }

    Value arg = NULL_VAL;
    if(argc) {
        arg = pop(vm);
    }

    Frame* frame = getFrame(vm);
    reserveStack(vm, gen->frame.stackTop);
    vm->sp = restoreFrame(gen, vm->sp - 1, frame);

    ObjModule* oldModule = vm->module;
    vm->module = gen->closure->fn->proto.module;

    if(!checkStackOverflow(vm)) {
        return false;
    }

    switch(action) {
    case GEN_SEND:
        if(gen->state == GEN_SUSPENDED) {
            push(vm, arg);
        }
        gen->state = GEN_RUNNING;
        return true;
    case GEN_THROW:
        push(vm, arg);
        jsrRaiseException(vm, -1);
        gen->state = GEN_RUNNING;
        return false;
    case GEN_CLOSE:
        // For enure handlers
        if(unwindHandlers(vm, frame, arg)) {
            return true;
        }

        closeUpvalues(vm, frame->stack);
        vm->sp = frame->stack;
        push(vm, arg);

        vm->frameCount--;
        vm->module = oldModule;
        gen->state = GEN_DONE;

        return true;
    }

    JSR_UNREACHABLE();
}

static bool invokeMethod(JStarVM* vm, ObjClass* cls, ObjString* name, uint8_t argc) {
    Value method;
    if(!hashTableValueGet(&cls->methods, name, &method)) {
        jsrRaise(vm, "MethodException", "Method %s.%s() doesn't exists", cls->name->data,
                 name->data);
        return false;
    }
    return callValue(vm, method, argc);
}

static bool invokeMethodCached(JStarVM* vm, ObjClass* cls, ObjString* name, uint8_t argc,
                               SymbolCache* sym) {
    if(isSymbolCached(vm, (Obj*)cls, sym)) {
        JSR_ASSERT(sym->type == SYMBOL_METHOD, "Invalid symbol type");
        return callValue(vm, sym->as.method, argc);
    }

    Value method;
    if(!hashTableValueGet(&cls->methods, name, &method)) {
        jsrRaise(vm, "MethodException", "Method %s.%s() doesn't exists", cls->name->data,
                 name->data);
        return false;
    }

    sym->type = SYMBOL_METHOD;
    sym->key = (Obj*)cls;
    sym->as.method = method;

    return callValue(vm, method, argc);
}

static bool bindMethod(JStarVM* vm, ObjClass* cls, ObjString* name) {
    Value v;
    if(!hashTableValueGet(&cls->methods, name, &v)) {
        jsrRaise(vm, "MethodException", "Method %s.%s() doesn't exists", cls->name->data,
                 name->data);
        return false;
    }

    ObjBoundMethod* boundMeth = newBoundMethod(vm, peek(vm), AS_OBJ(v));
    pop(vm);

    push(vm, OBJ_VAL(boundMeth));
    return true;
}

static bool bindMethodCached(JStarVM* vm, ObjClass* cls, ObjString* name, SymbolCache* sym) {
    if(isSymbolCached(vm, (Obj*)cls, sym)) {
        JSR_ASSERT(sym->type == SYMBOL_METHOD, "Invalid symbol type");
        push(vm, OBJ_VAL(newBoundMethod(vm, peek(vm), AS_OBJ(sym->as.method))));
        return true;
    }

    Value method;
    if(!hashTableValueGet(&cls->methods, name, &method)) {
        return false;
    }

    sym->type = SYMBOL_BOUND_METHOD;
    sym->key = (Obj*)cls;
    sym->as.method = method;

    ObjBoundMethod* boundMeth = newBoundMethod(vm, peek(vm), AS_OBJ(method));
    pop(vm);

    push(vm, OBJ_VAL(boundMeth));
    return true;
}

static bool checkSliceIndex(JStarVM* vm, ObjTuple* slice, size_t size, size_t* low, size_t* high) {
    if(slice->count != 2) {
        jsrRaise(vm, "TypeException", "Slice index must have two elements.");
        return false;
    }

    if(!IS_INT(slice->items[0]) || !IS_INT(slice->items[1])) {
        jsrRaise(vm, "TypeException", "Slice index must be two integers.");
        return false;
    }

    size_t a = jsrCheckIndexNum(vm, AS_NUM(slice->items[0]), size + 1);
    if(a == SIZE_MAX) return false;
    size_t b = jsrCheckIndexNum(vm, AS_NUM(slice->items[1]), size + 1);
    if(b == SIZE_MAX) return false;

    if(a > b) {
        jsrRaise(vm, "InvalidArgException",
                 "Invalid slice indices (%g, %g), first must be <= than second",
                 AS_NUM(slice->items[0]), AS_NUM(slice->items[1]));
        return false;
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
        push(vm, lst->items[idx]);
        return true;
    }
    if(IS_TUPLE(arg)) {
        size_t low = 0, high = 0;
        if(!checkSliceIndex(vm, AS_TUPLE(arg), lst->count, &low, &high)) return false;

        ObjList* ret = newList(vm, high - low);
        ret->count = high - low;
        for(size_t i = low; i < high; i++) {
            ret->items[i - low] = lst->items[i];
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
        size_t idx = jsrCheckIndexNum(vm, AS_NUM(arg), tup->count);
        if(idx == SIZE_MAX) return false;

        pop(vm), pop(vm);
        push(vm, tup->items[idx]);
        return true;
    }
    if(IS_TUPLE(arg)) {
        size_t low = 0, high = 0;
        if(!checkSliceIndex(vm, AS_TUPLE(arg), tup->count, &low, &high)) return false;

        ObjTuple* ret = newTuple(vm, high - low);
        for(size_t i = low; i < high; i++) {
            ret->items[i - low] = tup->items[i];
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

static bool binOverload(JStarVM* vm, const char* op, SpecialMethod overload,
                        SpecialMethod reverse) {
    Value method;
    ObjClass* cls1 = getClass(vm, peek2(vm));

    if(hashTableValueGet(&cls1->methods, vm->specialMethods[overload], &method)) {
        return callValue(vm, method, 1);
    }

    ObjClass* cls2 = getClass(vm, peek(vm));
    if(reverse != METH_SIZE) {
        swapStackSlots(vm, -1, -2);

        if(hashTableValueGet(&cls2->methods, vm->specialMethods[reverse], &method)) {
            return callValue(vm, method, 1);
        }
    }

    jsrRaise(vm, "TypeException", "Operator %s not defined for types %s, %s", op, cls1->name->data,
             cls2->name->data);
    return false;
}

static bool unaryOverload(JStarVM* vm, const char* op, SpecialMethod overload) {
    Value method;
    ObjClass* cls = getClass(vm, peek(vm));

    if(hashTableValueGet(&cls->methods, vm->specialMethods[overload], &method)) {
        return callValue(vm, method, 0);
    }

    jsrRaise(vm, "TypeException", "Unary operator %s not defined for type %s", op, cls->name->data);
    return false;
}

static bool unpackArgs(JStarVM* vm, uint8_t* out) {
    ObjList* args = AS_LIST(pop(vm));
    size_t argsCount = args->count;

    if(argsCount >= UINT8_MAX) {
        jsrRaise(vm, "TypeException", "Too many arguments for function call: %zu", argsCount);
        return false;
    }

    *out = (uint8_t)argsCount;

    reserveStack(vm, argsCount + 1);
    for(size_t i = 0; i < argsCount; i++) {
        push(vm, args->items[i]);
    }

    return true;
}

static bool unpackObject(JStarVM* vm, Obj* o, uint8_t n) {
    size_t count;
    Value* array = getValues(o, &count);

    if(n > count) {
        jsrRaise(vm, "TypeException", "Too few values to unpack: expected %d, got %zu", n, count);
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

    JStarNativeReg* reg = m->registry;
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

static bool getChachedSymbol(JStarVM* vm, Obj* key, Obj* val, const SymbolCache* sym, Value* out) {
    if(!isSymbolCached(vm, key, sym)) {
        return false;
    }

    switch(sym->type) {
    case SYMBOL_METHOD:
        *out = sym->as.method;
        return true;
    case SYMBOL_BOUND_METHOD:
        *out = OBJ_VAL(newBoundMethod(vm, OBJ_VAL(val), AS_OBJ(sym->as.method)));
        return true;
    case SYMBOL_FIELD:
        return instanceGetFieldAtOffset((ObjInstance*)val, sym->as.offset, out);
    case SYMBOL_GLOBAL:
        moduleGetGlobalAtOffset((ObjModule*)key, sym->as.offset, out);
        return true;
    }

    JSR_UNREACHABLE();
}

static bool getCachedField(JStarVM* vm, ObjClass* cls, ObjInstance* inst, const SymbolCache* sym,
                           Value* out) {
    if(!isSymbolCached(vm, (Obj*)cls, sym)) return false;
    JSR_ASSERT(sym->type == SYMBOL_FIELD, "Cached symbol is not a field");
    return instanceGetFieldAtOffset(inst, sym->as.offset, out);
}

static bool getCachedGlobal(JStarVM* vm, ObjModule* mod, const SymbolCache* sym, Value* out) {
    if(!isSymbolCached(vm, (Obj*)mod, sym)) return false;
    JSR_ASSERT(sym->type == SYMBOL_GLOBAL, "Cached symbol is not a global");
    moduleGetGlobalAtOffset(mod, sym->as.offset, out);
    return true;
}

// -----------------------------------------------------------------------------
// VM API
// -----------------------------------------------------------------------------

void* defaultRealloc(void* ptr, size_t oldSz, size_t newSz) {
    (void)newSz;
    if(newSz == 0) {
        free(ptr);
        return NULL;
    }
    return realloc(ptr, newSz);
}

inline bool getValueField(JStarVM* vm, ObjString* name, SymbolCache* sym) {
    Value val = peek(vm);
    if(IS_OBJ(val)) {
        switch(AS_OBJ(val)->type) {
        case OBJ_INST: {
            ObjInstance* inst = AS_INSTANCE(val);
            ObjClass* cls = inst->base.cls;

            // Check if the name resolution has been cached
            Value field;
            if(getCachedField(vm, cls, inst, sym, &field)) {
                pop(vm);
                push(vm, field);
                return true;
            }

            // Try to find a field
            int off = instanceGetFieldOffset(vm, cls, inst, name);
            if(off != -1) {
                sym->type = SYMBOL_FIELD;
                sym->key = (Obj*)cls;
                sym->as.offset = off;

                pop(vm);
                push(vm, inst->fields[off]);
                return true;
            }

            // This is a fallback, do not cache the result in case the field is added later
            if(bindMethod(vm, cls, name)) {
                return true;
            }

            jsrRaise(vm, "FieldException", "Object %s doesn't have field `%s`.",
                     inst->base.cls->name->data, name->data);
            return false;
        }
        case OBJ_MODULE: {
            ObjModule* mod = AS_MODULE(val);

            // Check if the name resolution has been cached
            Value global;
            if(getCachedGlobal(vm, mod, sym, &global)) {
                pop(vm);
                push(vm, global);
                return true;
            }

            // Try to find global variable
            int off = moduleGetGlobalOffset(vm, mod, name);
            if(off != -1) {
                sym->type = SYMBOL_GLOBAL;
                sym->key = (Obj*)mod;
                sym->as.offset = off;

                pop(vm);
                push(vm, mod->globals[off]);
                return true;
            }

            // This is a fallback, do not cache the result in case the global is added later
            if(bindMethod(vm, mod->base.cls, name)) {
                return true;
            }

            jsrRaise(vm, "NameException", "Name `%s` is not defined in module %s", name->data,
                     mod->name->data);
            return false;
        }
        default:
            break;
        }
    }

    ObjClass* cls = getClass(vm, val);
    if(bindMethodCached(vm, cls, name, sym)) {
        return true;
    }

    jsrRaise(vm, "FieldException", "Object %s doesn't have field `%s`.", cls->name->data,
             name->data);
    return false;
}

inline bool setValueField(JStarVM* vm, ObjString* name, SymbolCache* sym) {
    Value val = pop(vm);
    if(IS_OBJ(val)) {
        switch(AS_OBJ(val)->type) {
        case OBJ_INST: {
            ObjInstance* inst = AS_INSTANCE(val);
            ObjClass* cls = inst->base.cls;

            if(isSymbolCached(vm, (Obj*)cls, sym)) {
                JSR_ASSERT(sym->type == SYMBOL_FIELD, "Cached symbol is not a field");
                instanceSetFieldAtOffset(vm, inst, sym->as.offset, peek(vm));
                return true;
            }

            int off = instanceSetField(vm, cls, inst, name, peek(vm));
            sym->type = SYMBOL_FIELD;
            sym->key = (Obj*)cls;
            sym->as.offset = off;
            return true;
        }
        case OBJ_MODULE: {
            ObjModule* mod = AS_MODULE(val);

            if(isSymbolCached(vm, (Obj*)mod, sym)) {
                JSR_ASSERT(sym->type == SYMBOL_GLOBAL, "Cached symbol is not a global");
                moduleSetGlobalAtOffset(vm, mod, sym->as.offset, peek(vm));
                return true;
            }

            int off = moduleSetGlobal(vm, mod, name, peek(vm));
            sym->type = SYMBOL_GLOBAL;
            sym->key = (Obj*)mod;
            sym->as.offset = off;
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

inline bool getValueSubscript(JStarVM* vm) {
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

    if(!invokeMethod(vm, getClass(vm, peek2(vm)), vm->specialMethods[METH_GET], 1)) {
        return false;
    }
    return true;
}

inline bool setValueSubscript(JStarVM* vm) {
    if(IS_LIST(peek(vm))) {
        Value operand = pop(vm), arg = pop(vm), val = peek(vm);

        if(!IS_NUM(arg) || !isInt(AS_NUM(arg))) {
            jsrRaise(vm, "TypeException", "Index of List subscript access must be an integer.");
            return false;
        }

        ObjList* list = AS_LIST(operand);
        size_t index = jsrCheckIndexNum(vm, AS_NUM(arg), list->count);
        if(index == SIZE_MAX) return false;

        list->items[index] = val;
        return true;
    }

    // Swap operand and value to prepare function call
    swapStackSlots(vm, -1, -3);
    if(!invokeMethod(vm, getClass(vm, peekn(vm, 2)), vm->specialMethods[METH_SET], 2)) {
        return false;
    }
    return true;
}

inline bool callValue(JStarVM* vm, Value callee, uint8_t argc) {
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
            vm->sp[-argc - 1] = m->receiver;
            switch(m->method->type) {
            case OBJ_CLOSURE:
                return callFunction(vm, (ObjClosure*)m->method, argc);
            case OBJ_NATIVE:
                return callNative(vm, (ObjNative*)m->method, argc);
            default:
                JSR_UNREACHABLE();
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
            if(hashTableValueGet(&cls->methods, vm->specialMethods[METH_CTOR], &ctor)) {
                Obj* m = AS_OBJ(ctor);
                switch(m->type) {
                case OBJ_CLOSURE:
                    return callFunction(vm, (ObjClosure*)m, argc);
                case OBJ_NATIVE:
                    return callNative(vm, (ObjNative*)m, argc);
                default:
                    JSR_UNREACHABLE();
                }
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

inline bool invokeValue(JStarVM* vm, ObjString* name, uint8_t argc, SymbolCache* sym) {
    Value val = peekn(vm, argc);
    if(IS_OBJ(val)) {
        switch(AS_OBJ(val)->type) {
        case OBJ_INST: {
            ObjInstance* inst = AS_INSTANCE(val);
            ObjClass* cls = inst->base.cls;

            // Try cached method
            Value method;
            if(getChachedSymbol(vm, (Obj*)cls, (Obj*)inst, sym, &method)) {
                return callValue(vm, method, argc);
            }

            // Try to find a method
            if(hashTableValueGet(&cls->methods, name, &method)) {
                sym->type = SYMBOL_METHOD;
                sym->key = (Obj*)cls;
                sym->as.method = method;
                return callValue(vm, method, argc);
            }

            // If no method is found try a field
            int off = instanceGetFieldOffset(vm, cls, inst, name);
            if(off != -1) {
                sym->type = SYMBOL_FIELD;
                sym->key = (Obj*)cls;
                sym->as.offset = off;
                return callValue(vm, inst->fields[off], argc);
            }

            jsrRaise(vm, "MethodException", "Method %s.%s() doesn't exists", cls->name->data,
                     name->data);
            return false;
        }
        case OBJ_MODULE: {
            ObjModule* mod = AS_MODULE(val);

            Value func;
            if(getChachedSymbol(vm, (Obj*)mod, (Obj*)mod, sym, &func)) {
                if(sym->type != SYMBOL_METHOD) {
                    // This is a function call, put the function as the receiver instead of the
                    // module
                    vm->sp[-argc - 1] = func;
                }
                return callValue(vm, func, argc);
            }

            // Check if a method shadows a function in the module
            if(hashTableValueGet(&vm->modClass->methods, name, &func)) {
                sym->type = SYMBOL_METHOD;
                sym->key = (Obj*)mod->base.cls;
                sym->as.method = func;
                return callValue(vm, func, argc);
            }

            // If no method is found on the module object, try to get a global variable
            int off = moduleGetGlobalOffset(vm, mod, name);
            if(off != -1) {
                sym->type = SYMBOL_GLOBAL;
                sym->key = (Obj*)mod;
                sym->as.offset = off;

                // This is a function call, put the function as the receiver instead of the module
                Value func = mod->globals[off];
                vm->sp[-argc - 1] = func;
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
    return invokeMethodCached(vm, cls, name, argc, sym);
}

inline void setGlobalName(JStarVM* vm, ObjModule* mod, ObjString* name, SymbolCache* sym) {
    if(isSymbolCached(vm, (Obj*)mod, sym)) {
        JSR_ASSERT(sym->type == SYMBOL_GLOBAL, "Invalid symbol type");
        mod->globals[sym->as.offset] = peek(vm);
    } else {
        int off = moduleSetGlobal(vm, mod, name, peek(vm));
        sym->type = SYMBOL_GLOBAL;
        sym->key = (Obj*)mod;
        sym->as.offset = off;
    }
}

inline bool getGlobalName(JStarVM* vm, ObjModule* mod, ObjString* name, SymbolCache* sym) {
    if(getCachedGlobal(vm, mod, sym, vm->sp)) {
        vm->sp++;
        return true;
    }

    int off = moduleGetGlobalOffset(vm, mod, name);
    if(off == -1) {
        jsrRaise(vm, "NameException", "Name `%s` is not defined in module `%s`.", name->data,
                 mod->name->data);
        return false;
    }

    sym->type = SYMBOL_GLOBAL;
    sym->key = (Obj*)mod;
    sym->as.offset = off;

    push(vm, mod->globals[off]);
    return true;
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
    size_t oldSz = vm->stackSz;
    vm->stackSz = powerOf2Ceil(vm->stackSz + needed);
    vm->stack = vm->realloc(vm->stack, oldSz * sizeof(Value), vm->stackSz * sizeof(Value));
    JSR_ASSERT(vm->stack, "Out of memory");

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

// -----------------------------------------------------------------------------
// EVAL LOOP
// -----------------------------------------------------------------------------

bool runEval(JStarVM* vm, int evalDepth) {
    PROFILE_FUNC()

    // Keep frequently used variables in locals
    Frame* frame;
    Value* frameStack;
    ObjClosure* closure;
    ObjFunction* fn;
    uint8_t* ip;

    JSR_ASSERT(vm->frameCount != 0, "No frame to evaluate");
    JSR_ASSERT(vm->frameCount >= evalDepth, "Too few frame to evaluate");

    // The following macros are intended to be used only in this function

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

#define GET_CONST()          (fn->code.consts.items[NEXT_SHORT()])
#define GET_STRING()         (AS_STRING(GET_CONST()))
#define GET_SYMBOL()         (&fn->code.symbols.items[NEXT_SHORT()])
#define GET_SYMBOL_NAME(sym) (AS_STRING(fn->code.consts.items[(sym)->constant]))

#define EXIT_EVAL()    goto exit_eval;
#define UNWIND_STACK() goto stack_unwind;

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
        if(!res) UNWIND_STACK();                            \
    } while(0)

#define BITWISE(name, op, overload, reverse)                                           \
    do {                                                                               \
        if(IS_NUM(peek(vm)) && IS_NUM(peek2(vm))) {                                    \
            double b = AS_NUM(pop(vm));                                                \
            double a = AS_NUM(pop(vm));                                                \
            if(!HAS_INT_REPR(a) || !HAS_INT_REPR(b)) {                                 \
                jsrRaise(vm, "TypeException", "Number has no integer representation"); \
                UNWIND_STACK();                                                        \
            }                                                                          \
            push(vm, NUM_VAL((int64_t)a op(int64_t) b));                               \
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
        if(!res) UNWIND_STACK();                     \
    } while(0)

#define CHECK_EVAL_BREAK(vm)                        \
    do {                                            \
        if(vm->evalBreak) {                         \
            vm->evalBreak = 0;                      \
            jsrRaise(vm, "ProgramInterrupt", NULL); \
            UNWIND_STACK();                         \
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

    if(++vm->reentrantCalls >= MAX_REENTRANT) {
        jsrRaise(vm, "StackOverflowException", "Exceeded maximum number of reentrant calls");
        UNWIND_STACK();
    }
    
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
            BINARY_OVERLOAD(+, METH_ADD, METH_RADD);
        }
        DISPATCH();
    }
    
    TARGET(OP_MOD): {
        if(IS_NUM(peek(vm)) && IS_NUM(peek2(vm))) {
            double b = AS_NUM(pop(vm));
            double a = AS_NUM(pop(vm));
            push(vm, NUM_VAL(fmod(a, b)));
        } else {
            BINARY_OVERLOAD(%, METH_MOD, METH_RMOD);
        }
        DISPATCH();
    }
            
    TARGET(OP_POW): {
        if(IS_NUM(peek(vm)) && IS_NUM(peek2(vm))) {
            double y = AS_NUM(pop(vm));
            double x = AS_NUM(pop(vm));
            push(vm, NUM_VAL(pow(x, y)));
        } else {
            BINARY_OVERLOAD(^, METH_POW, METH_RPOW);
        }
        DISPATCH();
    }

    TARGET(OP_EQ): {
        if(IS_NUM(peek2(vm)) || IS_NULL(peek2(vm)) || IS_BOOL(peek2(vm))) {
            push(vm, BOOL_VAL(valueEquals(pop(vm), pop(vm))));
        } else {
            BINARY_OVERLOAD(==, METH_EQ, METH_SIZE);
        }
        DISPATCH();
    }

    TARGET(OP_INVERT): {
        if(IS_NUM(peek(vm))) {
            double x = AS_NUM(pop(vm));
            if(!HAS_INT_REPR(x)) {
                jsrRaise(vm, "TypeException", "Number has no integer representation");
                UNWIND_STACK();
            }
            push(vm, NUM_VAL(~(int64_t)x));
        } else {
            UNARY_OVERLOAD(NUM_VAL, ~, METH_INV);
        }
        DISPATCH();
    }

    TARGET(OP_NOT): {
        push(vm, BOOL_VAL(!valueToBool(pop(vm))));
        DISPATCH();
    }

    TARGET(OP_SUB):    BINARY(NUM_VAL, -, METH_SUB, METH_RSUB);
    TARGET(OP_MUL):    BINARY(NUM_VAL, *, METH_MUL, METH_RMUL);
    TARGET(OP_DIV):    BINARY(NUM_VAL, /, METH_DIV, METH_RDIV);
    TARGET(OP_LT):     BINARY(BOOL_VAL, <, METH_LT, METH_SIZE);
    TARGET(OP_LE):     BINARY(BOOL_VAL, <=, METH_LE, METH_SIZE);
    TARGET(OP_GT):     BINARY(BOOL_VAL, >, METH_GT, METH_SIZE);
    TARGET(OP_GE):     BINARY(BOOL_VAL, >=, METH_GE, METH_SIZE);
    TARGET(OP_LSHIFT): BITWISE(<<, <<, METH_LSHFT, METH_RLSHFT);
    TARGET(OP_RSHIFT): BITWISE(>>, >>, METH_RSHFT, METH_RRSHFT);
    TARGET(OP_BAND):   BITWISE(&, &, METH_BAND, METH_RBAND);
    TARGET(OP_BOR):    BITWISE(|, |, METH_BOR, METH_RBOR);
    TARGET(OP_XOR):    BITWISE(~, ^, METH_XOR, METH_RXOR);
    TARGET(OP_NEG):    UNARY(NUM_VAL, -, METH_NEG);

    TARGET(OP_IS): {
        if(!IS_CLASS(peek(vm))) {
            jsrRaise(vm, "TypeException", "Right operand of `is` must be a Class");
            UNWIND_STACK();
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
        if(!res) UNWIND_STACK();
        DISPATCH();
    }

    TARGET(OP_SUBSCR_SET): {
        SAVE_STATE();
        bool res = setValueSubscript(vm);
        LOAD_STATE();
        if(!res) UNWIND_STACK();
        DISPATCH();
    }

    TARGET(OP_GET_FIELD): {
        Symbol* sym = GET_SYMBOL();
        ObjString* name = GET_SYMBOL_NAME(sym);
        if(!getValueField(vm, name, &sym->cache)) {
            UNWIND_STACK();
        }
        DISPATCH();
    }

    TARGET(OP_SET_FIELD): {
        Symbol* sym = GET_SYMBOL();
        ObjString* name = GET_SYMBOL_NAME(sym);
        if(!setValueField(vm, name, &sym->cache)) {
            UNWIND_STACK();
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
        if(!hashTableValueGet(&cls->methods, vm->specialMethods[METH_ITER], &vm->sp[0]) ||
           !hashTableValueGet(&cls->methods, vm->specialMethods[METH_NEXT], &vm->sp[1])) {
            jsrRaise(vm, "MethodException", "Class %s does not implement __iter__ and __next__",
                     cls->name->data);
            UNWIND_STACK();
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
        if(!res) UNWIND_STACK();
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
            if(!res) UNWIND_STACK();
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
        if(!unpackArgs(vm, &argc)) {
            UNWIND_STACK();
        }
        goto call;

    TARGET(OP_CALL):
        argc = NEXT_CODE();
        goto call;

call:
        SAVE_STATE();
        bool res = callValue(vm, peekn(vm, argc), argc);
        LOAD_STATE();
        if(!res) UNWIND_STACK();
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
        if(!unpackArgs(vm, &argc)) {
            UNWIND_STACK();
        }
        goto invoke;

    TARGET(OP_INVOKE):
        argc = NEXT_CODE();
        goto invoke;

invoke:;
        Symbol* sym = GET_SYMBOL();
        ObjString* name = GET_SYMBOL_NAME(sym);
        SAVE_STATE();
        bool res = invokeValue(vm, name, argc, &sym->cache);
        LOAD_STATE();
        if(!res) UNWIND_STACK();
        DISPATCH();
    }
    
    {
        uint8_t argc;
        ObjClass* superCls;

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
        goto super_get_class;

    TARGET(OP_SUPER_UNPACK): {
        superCls = AS_CLASS(pop(vm));

        if(!unpackArgs(vm, &argc)) {
            UNWIND_STACK();
        }

        goto super_invoke;
    }

    TARGET(OP_SUPER):
        argc = NEXT_CODE();
        goto super_get_class;

super_get_class:;
        superCls = AS_CLASS(pop(vm));

super_invoke:;
        Symbol* sym = GET_SYMBOL();
        ObjString* name = GET_SYMBOL_NAME(sym);
        SAVE_STATE();
        bool res = invokeMethodCached(vm, superCls, name, argc, &sym->cache);
        LOAD_STATE();
        if(!res) UNWIND_STACK();
        DISPATCH();
    }

    TARGET(OP_SUPER_BIND): {
        Symbol* sym = GET_SYMBOL();
        ObjString* name = GET_SYMBOL_NAME(sym);
        ObjClass* superCls = AS_CLASS(pop(vm));
        if(!bindMethodCached(vm, superCls, name, &sym->cache)) {
            jsrRaise(vm, "MethodException", "Method %s.%s() doesn't exists", superCls->name->data,
                     name->data);
            UNWIND_STACK();
        }
        DISPATCH();
    }

op_return:
    TARGET(OP_RETURN): {
        Value ret = pop(vm);
        CHECK_EVAL_BREAK(vm);

        if(unwindHandlers(vm, frame, ret)) {
            LOAD_STATE();
            DISPATCH();
        }

        closeUpvalues(vm, frameStack);
        vm->sp = frameStack;
        push(vm, ret);

        if(--vm->frameCount == evalDepth) {
            EXIT_EVAL();
        }

        LOAD_STATE();
        vm->module = fn->proto.module;

        DISPATCH();
    }

    TARGET(OP_YIELD): {
        JSR_ASSERT(frame->gen, "Current function is not a Generator");
        JSR_ASSERT(frame->gen->state != GEN_STARTED && frame->gen->state != GEN_SUSPENDED,
                   "Invalid generator state");

        Value ret = pop(vm);

        ObjGenerator* gen = frame->gen;
        saveFrame(frame->gen, ip, vm->sp, frame);
        gen->state = GEN_SUSPENDED;
        gen->lastYield = ret;

        closeUpvalues(vm, frameStack);
        vm->sp = frameStack;
        push(vm, ret);

        if(--vm->frameCount == evalDepth) {
            EXIT_EVAL();
        }

        LOAD_STATE();
        vm->module = fn->proto.module;

        DISPATCH();
    }

    TARGET(OP_IMPORT): 
    TARGET(OP_IMPORT_FROM): {
        ObjString* name = GET_STRING();

        // The import callback in user code could be reentrant, so
        // we need to save and restore the state during an import
        SAVE_STATE();
        ObjModule* module = importModule(vm, name);
        LOAD_STATE();

        if(module == NULL) {
            jsrRaise(vm, "ImportException", "Cannot load module `%s`.", name->data);
            UNWIND_STACK();
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
        if(!moduleGetGlobal(vm, module, name, vm->sp)) {
            jsrRaise(vm, "NameException", "Name `%s` not defined in module `%s`.", name->data,
                     module->name->data);
            UNWIND_STACK();
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

    TARGET(OP_LIST_TO_TUPLE): {
        JSR_ASSERT(IS_LIST(peek(vm)), "Top of stack isn't a List");
        ObjList* lst = AS_LIST(peek(vm));
        ObjTuple* tup = newTuple(vm, lst->count);
        memcpy(tup->items, lst->items, sizeof(Value) * lst->count);
        pop(vm);
        push(vm, OBJ_VAL(tup));
        DISPATCH();
    }
    
    TARGET(OP_NEW_TUPLE): {
        uint8_t size = NEXT_CODE();
        ObjTuple* t = newTuple(vm, size);
        for(int i = size - 1; i >= 0; i--) {
            t->items[i] = pop(vm);
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
                c->upvalues[i] = closure->upvalues[index];
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

    TARGET(OP_GET_OBJECT): {
        push(vm, OBJ_VAL(vm->objClass));
        DISPATCH();
    }

    TARGET(OP_NEW_CLASS): {
        createClass(vm, GET_STRING());
        DISPATCH();
    }

    TARGET(OP_SUBCLASS): {
        if(!IS_CLASS(peek2(vm))) {
            jsrRaise(vm, "TypeException", "Superclass in class declaration must be a Class.");
            UNWIND_STACK();
        }

        ObjClass* superCls = AS_CLASS(peek2(vm));
        if(isBuiltinClass(vm, superCls)) {
            jsrRaise(vm, "TypeException", "Cannot subclass builtin class %s", superCls->name->data);
            UNWIND_STACK();
        }

        ObjClass* cls = AS_CLASS(peek(vm));
        cls->superCls = superCls;
        hashTableValueMerge(&cls->methods, &superCls->methods);

        DISPATCH();
    }

    TARGET(OP_UNPACK): {
        if(!IS_LIST(peek(vm)) && !IS_TUPLE(peek(vm))) {
            jsrRaise(vm, "TypeException", "Can unpack only Tuple or List, got %s.",
                     getClass(vm, peek(vm))->name->data);
            UNWIND_STACK();
        }
        if(!unpackObject(vm, AS_OBJ(pop(vm)), NEXT_CODE())) {
            UNWIND_STACK();
        }
        DISPATCH();
    }
    
    // TODO: rework super with upvalue
    TARGET(OP_DEF_METHOD): {
        ObjClass* cls = AS_CLASS(peek2(vm));
        ObjString* methodName = GET_STRING();
        hashTableValuePut(&cls->methods, methodName, pop(vm));
        DISPATCH();
    }

    TARGET(OP_NATIVE):
    TARGET(OP_NATIVE_METHOD): {
        ObjString* method = GET_STRING();
        ObjNative* native = AS_NATIVE(GET_CONST());

        const char* className = NULL;
        if(op == OP_NATIVE_METHOD) {
            ObjClass* cls = AS_CLASS(peek(vm));
            className = cls->name->data;
        }

        native->fn = resolveNative(vm->module, className, method->data);
        if(!native->fn) {
            jsrRaise(vm, "Exception", "Cannot resolve native %s.%s().", vm->module->name->data,
                     native->proto.name->data);
            UNWIND_STACK();
        }

        push(vm, OBJ_VAL(native));
        DISPATCH();
    }
    
    TARGET(OP_GET_CONST): {
        push(vm, GET_CONST());
        DISPATCH();
    }

    TARGET(OP_DEFINE_GLOBAL): {
        Symbol* sym = GET_SYMBOL();
        ObjString* name = GET_SYMBOL_NAME(sym);
        setGlobalName(vm, vm->module, name, &sym->cache);
        pop(vm);
        DISPATCH();
    }

    TARGET(OP_SET_GLOBAL): {
        Symbol* sym = GET_SYMBOL();
        ObjString* name = GET_SYMBOL_NAME(sym);
        setGlobalName(vm, vm->module, name, &sym->cache);
        DISPATCH();
    }

    TARGET(OP_GET_GLOBAL): {
        Symbol* sym = GET_SYMBOL();
        ObjString* name = GET_SYMBOL_NAME(sym);
        if(!getGlobalName(vm, vm->module, name, &sym->cache)) {
            UNWIND_STACK();
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
            JSR_ASSERT(IS_NUM(peek(vm)), "Top of stack isn't an unwind cause");
            UnwindCause cause = AS_NUM(pop(vm));
            JSR_ASSERT(cause == CAUSE_EXCEPT || cause == CAUSE_RETURN, "Invalid cause value");
            switch(cause) {
            case CAUSE_EXCEPT:
                UNWIND_STACK(); // Continue unwinding
                break;
            case CAUSE_RETURN:
                if(frame->gen) frame->gen->state = GEN_DONE;
                goto op_return; // Return will execute ensure handlers
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
        UNWIND_STACK();
    }

    TARGET(OP_GENERATOR_CLOSE): {
        JSR_ASSERT(frame->gen, "Current function isn't a Generator");
        frame->gen->state = GEN_DONE;
        DISPATCH();
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
        JSR_UNREACHABLE();
    }

    }

    // clang-format on

    JSR_UNREACHABLE();

stack_unwind:
    SAVE_STATE();
    if(!unwindStack(vm, evalDepth)) {
        vm->reentrantCalls--;
        return false;
    }
    LOAD_STATE();
    DISPATCH();

exit_eval:
    vm->reentrantCalls--;
    return true;
}

bool unwindStack(JStarVM* vm, int depth) {
    PROFILE_FUNC()

    JSR_ASSERT(vm->frameCount > depth, "No frame to unwind");
    JSR_ASSERT(isInstance(vm, peek(vm), vm->excClass), "Top of stack is not an Exception");

    ObjInstance* exception = AS_INSTANCE(peek(vm));
    ObjClass* cls = exception->base.cls;

    Value stacktraceVal = NULL_VAL;
    instanceGetField(vm, cls, exception, vm->excTrace, &stacktraceVal);

    JSR_ASSERT(IS_STACK_TRACE(stacktraceVal), "Exception doesn't have a stacktrace object");
    ObjStackTrace* stacktrace = AS_STACK_TRACE(stacktraceVal);

    Frame* frame = NULL;
    for(; vm->frameCount > depth; vm->frameCount--) {
        frame = &vm->frames[vm->frameCount - 1];

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
            JSR_UNREACHABLE();
        }

        stacktraceDump(vm, stacktrace, frame, vm->frameCount);

        // Execute exception handlers if present
        if(frame->handlerCount > 0) {
            Value exc = pop(vm);
            Handler* h = &frame->handlers[--frame->handlerCount];
            restoreHandler(vm, frame, h, CAUSE_EXCEPT, exc);
            return true;
        }

        // Set generators as completed
        if(frame->gen) {
            frame->gen->state = GEN_DONE;
        }

        closeUpvalues(vm, frame->stack);
    }

    // We have reached the end of the stack or a native/function boundary,
    // return from evaluation leaving the exception on top of the stack
    Value exc = pop(vm);
    vm->sp = frame->stack;
    push(vm, exc);

    return false;
}

// Inline function declarations
extern inline void push(JStarVM* vm, Value v);
extern inline Value pop(JStarVM* vm);
extern inline Value peek(const JStarVM* vm);
extern inline Value peek2(const JStarVM* vm);
extern inline void swapStackSlots(JStarVM* vm, int a, int b);
extern inline Value peekn(const JStarVM* vm, int n);
extern inline ObjClass* getClass(const JStarVM* vm, Value v);
extern inline bool isSubClass(const JStarVM* vm, ObjClass* sub, ObjClass* super);
extern inline bool isInstance(const JStarVM* vm, Value i, ObjClass* cls);
