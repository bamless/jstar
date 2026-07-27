#ifndef VM_H
#define VM_H

#include <signal.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "compiler.h"
#include "jstar.h"
#include "jstar_limits.h"
#include "object.h"
#include "parse/ast.h"
#include "symbol.h"
#include "value.h"
#include "value_hashtable.h"

// Enum encoding special method names needed at runtime.
// Mainly used for operator overloading.
typedef enum {
#define SPECIAL_METHOD(meth, _) meth,
#include "special_methods.def"
    SPECIAL_METHOD_COUNT,
} SpecialMethodId;

// Enum encoding the core classes of J*.
typedef enum {
    CORE_CLASS_CLASS,
    CORE_CLASS_OBJECT,
    CORE_CLASS_STR,
    CORE_CLASS_BOOL,
    CORE_CLASS_LIST,
    CORE_CLASS_NUMBER,
    CORE_CLASS_FUNCTION,
    CORE_CLASS_GENERATOR,
    CORE_CLASS_MODULE,
    CORE_CLASS_NULL,
    CORE_CLASS_STACKTRACE,
    CORE_CLASS_TUPLE,
    CORE_CLASS_TABLE,
    CORE_CLASS_USERDATA,
    CORE_CLASS_COUNT,
} CoreClass;

// Struct that stores the info needed to
// jump to handler code and to restore the
// VM state when handling exceptions.
typedef struct Handler {
    enum {
        HANDLER_ENSURE,
        HANDLER_EXCEPT,
    } type;            // The type of the handler block
    uint8_t* address;  // The address of handler code
    Value* savedSp;    // Saved stack state before the try block was enterd
} Handler;

// Stackframe of a function executing in
// the virtual machine.
typedef struct Frame {
    uint8_t* ip;                     // Instruction pointer
    Value* stack;                    // Base of stack for current frame
    Obj* fn;                         // Function associated with the frame (ObjClosure or ObjNative)
    ObjGenerator* gen;               // Generator of this frame (if any)
    Handler handlers[MAX_HANDLERS];  // Exception handlers
    uint8_t handlerCount;            // Exception handlers count
} Frame;

// The J* VM. This struct stores all the
// state needed to execute J* code.
struct JStarVM {
    // Core classes
    ObjClass* coreClasses[CORE_CLASS_COUNT];

    // Reference to the built-in `Exception` class
    ObjClass* excClass;

    // Script arguments
    ObjList* argv;

    // Current VM compiler (if any)
    Compiler* currCompiler;

    // Cached special method names needed at runtime
    ObjString* specialMethods[SPECIAL_METHOD_COUNT];

    // The empty tuple (singleton)
    ObjTuple* emptyTup;

    // Cached strings for exception fields
    ObjString *excErr, *excTrace, *excCause;

    // Loaded modules
    ValueHashTable modules;

    // Core module; initialized after `jsrInitRuntime`
    ObjModule* core;

    // VM program stack and stack pointer
    size_t stackSz;
    Value *stack, *sp;

    // Frame stack
    Frame* frames;
    int frameSz, frameCount;

    // Number of reentrant calls made into the VM
    int reentrantCalls;

    // Stack used during native function calls
    Value* apiStack;

    // Constant string pool, for interned strings
    ValueHashTable stringPool;

    // Linked list of all open upvalues
    ObjUpvalue* upvalues;

    // Callback function to report errors
    JStarErrorCB errorCallback;

    // Callback used to resolve `import`s
    JStarImportCB importCallback;

    // Allocation function
    JStarRealloc realloc;

    // If set, the VM will break the eval loop as soon as possible.
    // Can be set asynchronously by a signal handler
    volatile sig_atomic_t evalBreak;

    // Custom data associated with the VM
    void* userData;

    // Linked list of all created symbols
    JStarSymbol* symbols;

#ifdef JSTAR_DBG_CACHE_STATS
    size_t cacheHits, cacheMisses;
#endif

    // ---- Memory management ----

    // AST arena used in parsing
    JStarASTArena astArena;

    // Linked list of all allocated objects (used in
    // the sweep phase of GC to free unreached objects)
    Obj* objects;

    size_t allocated;  // Bytes currently allocated
    size_t nextGC;     // Bytes at which the next GC will be triggered
    int heapGrowRate;  // Rate at which the heap will grow after a GC

    // Stack used to recursevely reach all the fields of reached objects
    struct {
        Obj** items;
        size_t capacity, count;
    } reachedStack;
};

// Represents a handle to a resolved method, field or global variable.
// Internally it stores a cache of the symbol lookup.
struct JStarSymbol {
    SymbolCache sym;
    JStarSymbol* next;
    JStarSymbol* prev;
};

void* defaultRealloc(void* ptr, size_t oldSz, size_t newSz);

bool getValueField(JStarVM* vm, ObjString* name, SymbolCache* sym);
bool setValueField(JStarVM* vm, ObjString* name, SymbolCache* sym);
bool getGlobalName(JStarVM* vm, ObjModule* mod, ObjString* name, SymbolCache* sym);
void setGlobalName(JStarVM* vm, ObjModule* mod, ObjString* name, SymbolCache* sym);
bool getValueSubscript(JStarVM* vm);
bool setValueSubscript(JStarVM* vm);

bool callValue(JStarVM* vm, Value callee, uint8_t argc);
bool invokeValue(JStarVM* vm, ObjString* name, uint8_t argc, SymbolCache* sym);

void reserveStack(JStarVM* vm, size_t needed);
ObjModule* getCurrentModule(JStarVM* vm);

bool runEval(JStarVM* vm, int evalDepth);
bool unwindStack(JStarVM* vm, int toDepth);

inline void push(JStarVM* vm, Value v) {
    *vm->sp++ = v;
}

inline Value pop(JStarVM* vm) {
    return *--vm->sp;
}

inline Value peek(const JStarVM* vm) {
    return vm->sp[-1];
}

inline Value peek2(const JStarVM* vm) {
    return vm->sp[-2];
}

inline Value peekn(const JStarVM* vm, int n) {
    return vm->sp[-(n + 1)];
}

inline void swapStackSlots(JStarVM* vm, int a, int b) {
    Value tmp = vm->sp[a];
    vm->sp[a] = vm->sp[b];
    vm->sp[b] = tmp;
}

inline ObjClass* getClass(const JStarVM* vm, Value v) {
#ifdef JSTAR_NAN_TAGGING
    if(IS_OBJ(v)) return AS_OBJ(v)->cls;
    if(IS_NUM(v)) return vm->coreClasses[CORE_CLASS_NUMBER];

    switch(BITS_TAG(v)) {
    case HANDLE_BITS:
    case NULL_BITS:
        return vm->coreClasses[CORE_CLASS_NULL];
    case FALSE_BITS:
    case TRUE_BITS:
        return vm->coreClasses[CORE_CLASS_BOOL];
    case END_BITS:
        JSR_UNREACHABLE();
    }
#else
    switch(v.type) {
    case VAL_NUM:
        return vm->numClass;
    case VAL_BOOL:
        return vm->boolClass;
    case VAL_OBJ:
        return AS_OBJ(v)->cls;
    case VAL_HANDLE:
    case VAL_NULL:
        return vm->nullClass;
    }
#endif

    JSR_UNREACHABLE();
}

inline bool isCoreClass(const JStarVM* vm, ObjClass* cls) {
    for(int c = 0; c < CORE_CLASS_COUNT; c++) {
        if(cls == vm->coreClasses[c]) return true;
    }
    return false;
}

inline bool isSubClass(ObjClass* sub, ObjClass* super) {
    for(ObjClass* c = sub; c != NULL; c = c->superCls) {
        if(c == super) {
            return true;
        }
    }
    return false;
}

inline bool isInstance(const JStarVM* vm, Value i, ObjClass* cls) {
    return isSubClass(getClass(vm, i), cls);
}

#endif
