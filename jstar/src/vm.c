#include "vm.h"

#include <math.h>
#include <string.h>

#include "builtin/modules.h"
#include "chunk.h"
#include "core.h"
#include "import.h"
#include "memory.h"
#include "opcode.h"

// Enumeration encoding the cause of the stack
// unwinding, used during unwinding to correctly
// handle the execution of except/ensure handlers
typedef enum UnwindCause {
    CAUSE_EXCEPT,
    CAUSE_RETURN,
} UnwindCause;

static void reset(JStarVM* vm) {
    vm->sp = vm->stack;
    vm->apiStack = vm->stack;
    vm->frameCount = 0;
    vm->module = NULL;
}

JStarVM* jsrNewVM(JStarConf* conf) {
    JStarVM* vm = calloc(1, sizeof(*vm));

    vm->stackSz = roundUp(conf->stackSize, MAX_LOCALS + 1);
    vm->frameSz = vm->stackSz / (MAX_LOCALS + 1);
    vm->stack = malloc(sizeof(Value) * vm->stackSz);
    vm->frames = malloc(sizeof(Frame) * vm->frameSz);

    vm->errorFun = conf->errorFun;

    reset(vm);
    initHashTable(&vm->modules);
    initHashTable(&vm->strings);

    // init GC
    vm->nextGC = conf->initGC;
    vm->heapGrowRate = conf->heapGrowRate;

    // Create constants strings
    vm->stacktrace = copyString(vm, EXC_M_STACKTRACE, strlen(EXC_M_STACKTRACE), true);
    vm->ctor = copyString(vm, CTOR_STR, strlen(CTOR_STR), true);

    vm->next = copyString(vm, "__next__", 8, true);
    vm->iter = copyString(vm, "__iter__", 8, true);

    vm->add = copyString(vm, "__add__", 7, true);
    vm->sub = copyString(vm, "__sub__", 7, true);
    vm->mul = copyString(vm, "__mul__", 7, true);
    vm->div = copyString(vm, "__div__", 7, true);
    vm->mod = copyString(vm, "__mod__", 7, true);
    vm->get = copyString(vm, "__get__", 7, true);
    vm->set = copyString(vm, "__set__", 7, true);

    vm->radd = copyString(vm, "__radd__", 8, true);
    vm->rsub = copyString(vm, "__rsub__", 8, true);
    vm->rmul = copyString(vm, "__rmul__", 8, true);
    vm->rdiv = copyString(vm, "__rdiv__", 8, true);
    vm->rmod = copyString(vm, "__rmod__", 8, true);

    vm->lt = copyString(vm, "__lt__", 6, true);
    vm->le = copyString(vm, "__le__", 6, true);
    vm->gt = copyString(vm, "__gt__", 6, true);
    vm->ge = copyString(vm, "__ge__", 6, true);
    vm->eq = copyString(vm, "__eq__", 6, true);

    vm->neg = copyString(vm, "__neg__", 7, true);

    // Bootstrap the core module
    initCoreModule(vm);

    // Init main module
    ObjString* mainModuleName = copyString(vm, JSR_MAIN_MODULE, strlen(JSR_MAIN_MODULE), true);
    compileWithModule(vm, "<main>", mainModuleName, NULL);

    // This is called after initCoreLibrary in order to correctly assign
    // classes to objects, since classes are created in intCoreLibrary
    vm->importpaths = newList(vm, 8);
    vm->emptyTup = newTuple(vm, 0);

    return vm;
}

void jsrFreeVM(JStarVM* vm) {
    reset(vm);

    free(vm->stack);
    free(vm->frames);
    freeHashTable(&vm->strings);
    freeHashTable(&vm->modules);
    freeObjects(vm);

#ifdef DBG_PRINT_GC
    printf("Allocated at exit: %lu bytes.\n", vm->allocated);
#endif

    free(vm);
}

static Frame* getFrame(JStarVM* vm, Callable* c) {
    if(vm->frameCount + 1 == vm->frameSz) {
        vm->frameSz *= 2;
        vm->frames = realloc(vm->frames, sizeof(Frame) * vm->frameSz);
    }

    Frame* callFrame = &vm->frames[vm->frameCount++];
    callFrame->stack = vm->sp - (c->argsCount + 1);
    callFrame->handlerc = 0;
    if(c->vararg) callFrame->stack--;

    return callFrame;
}

static void appendCallFrame(JStarVM* vm, ObjClosure* closure) {
    Frame* callFrame = getFrame(vm, &closure->fn->c);
    callFrame->fn = OBJ_VAL(closure);
    callFrame->ip = closure->fn->chunk.code;
}

static void appendNativeFrame(JStarVM* vm, ObjNative* native) {
    Frame* callFrame = getFrame(vm, &native->c);
    callFrame->fn = OBJ_VAL(native);
    callFrame->ip = NULL;
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
    if(prev == NULL)
        vm->upvalues = createdUpvalue;
    else
        prev->next = createdUpvalue;

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

static void raiseArgsExc(JStarVM* vm, Callable* function, int expected, int supplied,
                         const char* quantity) {
    jsrRaise(vm, "TypeException", "Function `%s.%s` takes %s %d arguments, %d supplied.",
             function->module->name->data, function->name->data, quantity, expected, supplied);
}

static bool adjustArguments(JStarVM* vm, Callable* c, uint8_t argc) {
    if(c->defaultc != 0) {
        uint8_t most = c->argsCount, least = most - c->defaultc;

        if((!c->vararg && argc > most) || argc < least) {
            bool tooMany = argc > most;
            raiseArgsExc(vm, c, tooMany ? most : least, argc, tooMany ? "at most" : "at least");
            return false;
        }

        // push remaining args taking the default value
        for(uint8_t i = argc - least; i < c->defaultc; i++) {
            push(vm, c->defaults[i]);
        }

        if(c->vararg) packVarargs(vm, argc > most ? argc - most : 0);
    } else if(c->vararg) {
        if(argc < c->argsCount) {
            raiseArgsExc(vm, c, c->argsCount, argc, "at least");
            return false;
        }
        packVarargs(vm, argc - c->argsCount);
    } else if(c->argsCount != argc) {
        raiseArgsExc(vm, c, c->argsCount, argc, "exactly");
        return false;
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
    jsrEnsureStack(vm, UINT8_MAX);
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

    jsrEnsureStack(vm, JSTAR_MIN_NATIVE_STACK_SZ);
    appendNativeFrame(vm, native);

    ObjModule* oldModule = vm->module;
    size_t apiStackOff = vm->apiStack - vm->stack;

    vm->module = native->c.module;
    vm->apiStack = vm->frames[vm->frameCount - 1].stack;

    if(!native->fn(vm)) {
        vm->module = oldModule;
        vm->apiStack = vm->stack + apiStackOff;
        return false;
    }

    Value ret = pop(vm);

    vm->frameCount--;
    vm->sp = vm->apiStack;
    vm->module = oldModule;
    vm->apiStack = vm->stack + apiStackOff;

    push(vm, ret);
    return true;
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
            if(m->method->type == OBJ_CLOSURE)
                return callFunction(vm, (ObjClosure*)m->method, argc);
            else
                return callNative(vm, (ObjNative*)m->method, argc);
        }
        case OBJ_CLASS: {
            ObjClass* cls = AS_CLASS(callee);

            if(isNonInstantiableBuiltin(vm, cls)) {
                jsrRaise(vm, "Exception", "class %s can't be directly instatiated",
                         cls->name->data);
                return false;
            }

            vm->sp[-argc - 1] = isInstatiableBuiltin(vm, cls) ? NULL_VAL
                                                              : OBJ_VAL(newInstance(vm, cls));

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

static bool invokeMethod(JStarVM* vm, ObjClass* cls, ObjString* name, uint8_t argc) {
    Value method;
    if(!hashTableGet(&cls->methods, name, &method)) {
        jsrRaise(vm, "MethodException", "Method %s.%s() doesn't exists", cls->name->data,
                 name->data);
        return false;
    }
    return callValue(vm, method, argc);
}

bool invokeValue(JStarVM* vm, ObjString* name, uint8_t argc) {
    Value val = peekn(vm, argc);
    if(IS_OBJ(val)) {
        switch(OBJ_TYPE(val)) {
        case OBJ_INST: {
            ObjInstance* inst = AS_INSTANCE(val);

            // Check if field shadows a method
            Value f;
            if(hashTableGet(&inst->fields, name, &f)) {
                return callValue(vm, f, argc);
            }

            return invokeMethod(vm, inst->base.cls, name, argc);
        }
        case OBJ_MODULE: {
            ObjModule* mod = AS_MODULE(val);

            Value func;
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
        default: {
            Obj* o = AS_OBJ(val);
            return invokeMethod(vm, o->cls, name, argc);
        }
        }
    }

    // if builtin type get the class and try to invoke
    ObjClass* cls = getClass(vm, val);
    return invokeMethod(vm, cls, name, argc);
}

bool getFieldFromValue(JStarVM* vm, Value val, ObjString* name) {
    if(IS_OBJ(val)) {
        switch(OBJ_TYPE(val)) {
        case OBJ_INST: {
            ObjInstance* inst = AS_INSTANCE(val);

            Value v;
            if(!hashTableGet(&inst->fields, name, &v)) {
                // if we didnt find a field try to return bound method
                if(!hashTableGet(&inst->base.cls->methods, name, &v)) {
                    jsrRaise(vm, "FieldException", "Object %s doesn't have field `%s`.",
                             inst->base.cls->name->data, name->data);
                    return false;
                }
                push(vm, OBJ_VAL(newBoundMethod(vm, val, AS_OBJ(v))));
                return true;
            }

            push(vm, v);
            return true;
        }
        case OBJ_MODULE: {
            ObjModule* mod = AS_MODULE(val);

            Value v;
            if(!hashTableGet(&mod->globals, name, &v)) {
                // if we didnt find a global name try to return bound method
                if(!hashTableGet(&mod->base.cls->methods, name, &v)) {
                    jsrRaise(vm, "NameException", "Name `%s` is not defined in module %s",
                             name->data, mod->name->data);
                    return false;
                }
                push(vm, OBJ_VAL(newBoundMethod(vm, val, AS_OBJ(v))));
                return true;
            }

            push(vm, v);
            return true;
        }
        default:
            break;
        }
    }

    Value v;
    ObjClass* cls = getClass(vm, val);
    if(!hashTableGet(&cls->methods, name, &v)) {
        jsrRaise(vm, "FieldException", "Object %s doesn't have field `%s`.", cls->name->data,
                 name->data);
        return false;
    }

    push(vm, OBJ_VAL(newBoundMethod(vm, val, AS_OBJ(v))));
    return true;
}

bool setFieldOfValue(JStarVM* vm, Value val, ObjString* name, Value s) {
    if(IS_OBJ(val)) {
        switch(OBJ_TYPE(val)) {
        case OBJ_INST: {
            ObjInstance* inst = AS_INSTANCE(val);
            hashTablePut(&inst->fields, name, s);
            return true;
        }
        case OBJ_MODULE: {
            ObjModule* mod = AS_MODULE(val);
            hashTablePut(&mod->globals, name, s);
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

static bool getSubscriptOfValue(JStarVM* vm, Value operand, Value arg) {
    if(IS_OBJ(operand)) {
        switch(OBJ_TYPE(operand)) {
        case OBJ_LIST: {
            if(!IS_NUM(arg) || !isInt(AS_NUM(arg))) {
                jsrRaise(vm, "TypeException", "Index of List subscript access must be an integer.");
                return false;
            }

            ObjList* list = AS_LIST(operand);
            size_t index = jsrCheckIndexNum(vm, AS_NUM(arg), list->count);
            if(index == SIZE_MAX) return false;

            push(vm, list->arr[index]);
            return true;
        }
        case OBJ_TUPLE: {
            if(!IS_NUM(arg) || !isInt(AS_NUM(arg))) {
                jsrRaise(vm, "TypeException", "Index of Tuple subscript must be an integer.");
                return false;
            }

            ObjTuple* tuple = AS_TUPLE(operand);
            size_t index = jsrCheckIndexNum(vm, AS_NUM(arg), tuple->size);
            if(index == SIZE_MAX) return false;

            push(vm, tuple->arr[index]);
            return true;
        }
        case OBJ_STRING: {
            if(!IS_NUM(arg) || !isInt(AS_NUM(arg))) {
                jsrRaise(vm, "TypeException", "Index of String subscript must be an integer.");
                return false;
            }

            ObjString* str = AS_STRING(operand);
            size_t index = jsrCheckIndexNum(vm, AS_NUM(arg), str->length);
            if(index == SIZE_MAX) return false;

            char character = str->data[index];
            push(vm, OBJ_VAL(copyString(vm, &character, 1, true)));
            return true;
        }
        default:
            break;
        }
    }

    push(vm, operand);
    push(vm, arg);
    if(!invokeMethod(vm, getClass(vm, operand), vm->get, 1)) {
        return false;
    }
    return true;
}

static bool setSubscriptOfValue(JStarVM* vm, Value operand, Value arg, Value s) {
    if(IS_LIST(operand)) {
        if(!IS_NUM(arg) || !isInt(AS_NUM(arg))) {
            jsrRaise(vm, "TypeException", "Index of List subscript access must be an integer.");
            return false;
        }

        ObjList* list = AS_LIST(operand);
        size_t index = jsrCheckIndexNum(vm, AS_NUM(arg), list->count);
        if(index == SIZE_MAX) return false;

        list->arr[index] = s;
        push(vm, s);
        return true;
    }

    push(vm, operand);
    push(vm, arg);
    push(vm, s);
    if(!invokeMethod(vm, getClass(vm, operand), vm->set, 2)) {
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

static bool callBinaryOverload(JStarVM* vm, ObjString* name, ObjString* reverse) {
    Value op;
    ObjClass* cls = getClass(vm, peek2(vm));
    if(hashTableGet(&cls->methods, name, &op)) {
        return callValue(vm, op, 1);
    }

    if(reverse) {
        // swap callee and arg
        Value b = peek(vm);
        vm->sp[-1] = vm->sp[-2];
        vm->sp[-2] = b;

        ObjClass* cls2 = getClass(vm, peek2(vm));
        if(hashTableGet(&cls2->methods, reverse, &op)) {
            return callValue(vm, op, 1);
        }
    }
    return false;
}

static bool unpackObject(JStarVM* vm, Obj* o, uint8_t n) {
    size_t size = 0;
    Value* arr = NULL;

    switch(o->type) {
    case OBJ_TUPLE:
        arr = ((ObjTuple*)o)->arr;
        size = ((ObjTuple*)o)->size;
        break;
    case OBJ_LIST:
        arr = ((ObjList*)o)->arr;
        size = ((ObjList*)o)->count;
        break;
    default:
        UNREACHABLE();
        break;
    }

    if(n > size) {
        jsrRaise(vm, "TypeException", "Too little values to unpack.");
        return false;
    }

    for(int i = 0; i < n; i++) {
        push(vm, arr[i]);
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

bool runEval(JStarVM* vm, int depth) {
    register Frame* frame;
    register Value* frameStack;
    register ObjClosure* closure;
    register ObjFunction* fn;
    register uint8_t* ip;

    ASSERT(vm->frameCount != 0, "No frame to evaluate");
    ASSERT(vm->frameCount >= depth, "Too few frame to evaluate");

#define LOAD_FRAME()                         \
    frame = &vm->frames[vm->frameCount - 1]; \
    frameStack = frame->stack;               \
    closure = AS_CLOSURE(frame->fn);         \
    fn = closure->fn;                        \
    ip = frame->ip;

#define SAVE_FRAME() frame->ip = ip;

#define NEXT_CODE()  (*ip++)
#define NEXT_SHORT() (ip += 2, ((uint16_t)ip[-2] << 8) | ip[-1])

#define GET_CONST()  (fn->chunk.consts.arr[NEXT_SHORT()])
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

#define BINARY_OVERLOAD(op, overload, reverse)                \
    do {                                                      \
        SAVE_FRAME();                                         \
        bool res = callBinaryOverload(vm, overload, reverse); \
        LOAD_FRAME();                                         \
        if(!res) {                                            \
            ObjString* t1 = getClass(vm, peek(vm))->name;     \
            ObjString* t2 = getClass(vm, peek2(vm))->name;    \
            jsrRaise(vm, "TypeException",                     \
                     "Operator %s not defined "               \
                     "for types %s, %s",                      \
                     #op, t1->data, t2->data);                \
            UNWIND_STACK(vm);                                 \
        }                                                     \
    } while(0)

#define RESTORE_HANDLER(h, frame, cause, excVal) \
    do {                                         \
        frame->ip = h->address;                  \
        vm->sp = h->savesp;                      \
        closeUpvalues(vm, vm->sp - 1);           \
        push(vm, excVal);                        \
        push(vm, NUM_VAL(cause));                \
    } while(0)

#define UNWIND_STACK(vm)              \
    do {                              \
        SAVE_FRAME()                  \
        if(!unwindStack(vm, depth)) { \
            return false;             \
        }                             \
        LOAD_FRAME();                 \
        DISPATCH();                   \
    } while(0)

#ifdef DBG_PRINT_EXEC
    #define PRINT_DBG_STACK()                        \
        printf("     ");                             \
        for(Value* v = vm->stack; v < vm->sp; v++) { \
            printf("[");                             \
            printValue(*v);                          \
            printf("]");                             \
        }                                            \
        printf("$\n");                               \
        disassembleIstr(&fn->chunk, (size_t)(ip - fn->chunk.code));
#else
    #define PRINT_DBG_STACK()
#endif

#ifdef USE_COMPUTED_GOTOS
    // create jumptable
    #define JMPTARGET(X) &&TARGET_##X,
    static void* opJmpTable[] = {OPCODE(JMPTARGET)};

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

    LOAD_FRAME();

    uint8_t op;
    DECODE(op) {

    TARGET(OP_ADD): {
        if(IS_NUM(peek(vm)) && IS_NUM(peek2(vm))) {
            double b = AS_NUM(pop(vm));
            double a = AS_NUM(pop(vm));
            push(vm, NUM_VAL(a + b));
        } else if(IS_STRING(peek(vm)) && IS_STRING(peek2(vm))) {
            ObjString* conc = stringConcatenate(vm, AS_STRING(peek2(vm)), AS_STRING(peek(vm)));
            pop(vm);
            pop(vm);
            push(vm, OBJ_VAL(conc));
        } else {
            BINARY_OVERLOAD(+, vm->add, vm->radd);
        }
        DISPATCH();
    }

    TARGET(OP_SUB): { 
        BINARY(NUM_VAL, -, vm->sub, vm->rsub);
        DISPATCH();
    }

    TARGET(OP_MUL): {
        BINARY(NUM_VAL, *, vm->mul, vm->rmul);
        DISPATCH();
    }

    TARGET(OP_DIV): {
        BINARY(NUM_VAL, /, vm->div, vm->rdiv);
        DISPATCH();
    }
    
    TARGET(OP_MOD): {
        if(IS_NUM(peek(vm)) && IS_NUM(peek2(vm))) {
            double b = AS_NUM(pop(vm));
            double a = AS_NUM(pop(vm));
            push(vm, NUM_VAL(fmod(a, b)));
        } else {
            BINARY_OVERLOAD(%, vm->mod, vm->rmod);
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
            SAVE_FRAME();
            bool res = invokeMethod(vm, cls, vm->neg, 0);
            LOAD_FRAME();
            if(!res) UNWIND_STACK(vm);
        }
        DISPATCH();
    }

    TARGET(OP_LT): {
        BINARY(BOOL_VAL, <,  vm->lt, NULL);
        DISPATCH();
    }

    TARGET(OP_LE): {
        BINARY(BOOL_VAL, <=, vm->le, NULL);
        DISPATCH();
    }

    TARGET(OP_GT): {
        BINARY(BOOL_VAL, >, vm->gt, NULL);
        DISPATCH();
    }

    TARGET(OP_GE): {
        BINARY(BOOL_VAL, >=, vm->ge, NULL);
        DISPATCH();
    }

    TARGET(OP_EQ): {
        if(IS_NUM(peek2(vm)) || IS_NULL(peek2(vm)) || IS_BOOL(peek2(vm))) {
            push(vm, BOOL_VAL(valueEquals(pop(vm), pop(vm))));
        } else {
            Value eq;
            ObjClass* cls = getClass(vm, peek2(vm));
            if(hashTableGet(&cls->methods, vm->eq, &eq)) {
                SAVE_FRAME();
                bool res = callValue(vm, eq, 1);
                LOAD_FRAME();
                if(!res) UNWIND_STACK(vm);
            }
        }
        DISPATCH();
    }

    TARGET(OP_NOT): {
        push(vm, BOOL_VAL(!isValTrue(pop(vm))));
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
        Value arg = pop(vm), operand = pop(vm);
        SAVE_FRAME();
        bool res = getSubscriptOfValue(vm, operand, arg);
        LOAD_FRAME();
        if(!res) UNWIND_STACK(vm);
        DISPATCH();
    }

    TARGET(OP_SUBSCR_SET): {
        Value arg = pop(vm), operand = pop(vm), s = pop(vm);
        SAVE_FRAME();
        bool res = setSubscriptOfValue(vm, operand, arg, s);
        LOAD_FRAME();
        if(!res) UNWIND_STACK(vm);
        DISPATCH();
    }

    TARGET(OP_GET_FIELD): {
        Value v = pop(vm);
        if(!getFieldFromValue(vm, v, GET_STRING())) {
            UNWIND_STACK(vm);
        }
        DISPATCH();
    }

    TARGET(OP_SET_FIELD): {
        Value v = pop(vm);
        if(!setFieldOfValue(vm, v, GET_STRING(), peek(vm))) {
            UNWIND_STACK(vm);
        }
        DISPATCH();
    }

    TARGET(OP_JUMP): {
        int16_t off = NEXT_SHORT();
        ip += off;
        DISPATCH();
    }

    TARGET(OP_JUMPF): {
        int16_t off = NEXT_SHORT();
        if(!isValTrue(pop(vm))) ip += off;
        DISPATCH();
    }

    TARGET(OP_JUMPT): {
        int16_t off = NEXT_SHORT();
        if(isValTrue(pop(vm))) ip += off;
        DISPATCH();
    }

    TARGET(OP_FOR_ITER): {
        vm->sp[0] = vm->sp[-2];
        vm->sp[1] = vm->sp[-1];
        vm->sp += 2;
        SAVE_FRAME();
        bool res = invokeValue(vm, vm->iter, 1);
        LOAD_FRAME();
        if(!res) UNWIND_STACK(vm);
        DISPATCH();
    }

    TARGET(OP_FOR_NEXT): {
        vm->sp[-2] = vm->sp[-1];
        int16_t off = NEXT_SHORT();
        if(isValTrue(pop(vm))) {
            vm->sp[0] = vm->sp[-2];
            vm->sp[1] = vm->sp[-1];
            vm->sp += 2;
            SAVE_FRAME();
            bool res = invokeValue(vm, vm->next, 1);
            LOAD_FRAME();
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

    TARGET(OP_CALL):
        argc = NEXT_CODE();

call:
        SAVE_FRAME();
        bool res = callValue(vm, peekn(vm, argc), argc);
        LOAD_FRAME();
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
    
    TARGET(OP_INVOKE):
        argc = NEXT_CODE();

invoke:;
        ObjString* name = GET_STRING();
        SAVE_FRAME();
        bool res = invokeValue(vm, name, argc);
        LOAD_FRAME();
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
        goto sup_invoke;

    TARGET(OP_SUPER):
        argc = NEXT_CODE();

sup_invoke:;
        ObjString* name = GET_STRING();
        // The superclass is stored as a const in the function itself
        ObjClass* sup = AS_CLASS(fn->chunk.consts.arr[0]);
        SAVE_FRAME();
        bool res = invokeMethod(vm, sup, name, argc);
        LOAD_FRAME();
        if(!res) UNWIND_STACK(vm);
        DISPATCH();
    }

op_return:
    TARGET(OP_RETURN): {
        Value ret = pop(vm);

        while(frame->handlerc > 0) {
            Handler* h = &frame->handlers[--frame->handlerc];
            if(h->type == HANDLER_ENSURE) {
                RESTORE_HANDLER(h, frame, CAUSE_RETURN, ret);
                LOAD_FRAME();
                DISPATCH();
            }
        }

        closeUpvalues(vm, frameStack);
        vm->sp = frameStack;
        push(vm, ret);

        if(--vm->frameCount == depth) {
            return true;
        }

        LOAD_FRAME();
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
            SAVE_FRAME();
            ObjClosure* c = newClosure(vm, AS_FUNC(peek(vm)));
            vm->sp[-1] = OBJ_VAL(c); 
            callFunction(vm, c, 0);
            LOAD_FRAME();
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
        for(uint8_t i = 0; i < c->fn->upvaluec; i++) {
            uint8_t isLocal = NEXT_CODE();
            uint8_t index = NEXT_CODE();
            if(isLocal) {
                c->upvalues[i] = captureUpvalue(vm, frame->stack + index);
            } else {
                c->upvalues[i] = AS_CLOSURE(frame->fn)->upvalues[index];
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
    
    TARGET(OP_DEF_METHOD): {
        ObjClass* cls = AS_CLASS(peek2(vm));
        ObjString* methodName = GET_STRING();
        // Set the superclass as a const in the function
        AS_CLOSURE(peek(vm))->fn->chunk.consts.arr[0] = OBJ_VAL(cls->superCls);
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
            jsrRaise(vm, "Exception", "Cannot resolve native %s.", nat->c.name->data);
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
        if(!hashTableGet(&vm->module->globals, name, vm->sp++)) {
            jsrRaise(vm, "NameException", "Name `%s` is not defined.", name->data);
            UNWIND_STACK(vm);
        }
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
        ObjInstance* excInst = AS_INSTANCE(exc);
        hashTablePut(&excInst->fields, vm->stacktrace, OBJ_VAL(st));
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

    Value stackTraceValue = NULL_VAL;
    hashTableGet(&exception->fields, vm->stacktrace, &stackTraceValue);
    ASSERT(IS_STACK_TRACE(stackTraceValue), "Exception doesn't have a stacktrace object");
    ObjStackTrace* stackTrace = AS_STACK_TRACE(stackTraceValue);

    for(; vm->frameCount > depth; vm->frameCount--) {
        Frame* frame = &vm->frames[vm->frameCount - 1];

        if(IS_CLOSURE(frame->fn))
            vm->module = AS_CLOSURE(frame->fn)->fn->c.module;
        else
            vm->module = AS_NATIVE(frame->fn)->c.module;

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
