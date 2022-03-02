#ifndef VM_H
#define VM_H

#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include "compiler.h"
#include "hashtable.h"
#include "jstar.h"
#include "jstar_limits.h"
#include "object.h"
#include "util.h"
#include "value.h"

// Enum encoding special method names needed at runtime
// Mainly used for operator overloading
// See methodSyms array in vm.c
typedef enum MethodSymbol {
    SYM_CTOR,

    SYM_ITER,
    SYM_NEXT,

    SYM_ADD,
    SYM_SUB,
    SYM_MUL,
    SYM_DIV,
    SYM_MOD,
    SYM_BAND,
    SYM_BOR,
    SYM_XOR,
    SYM_LSHFT,
    SYM_RSHFT,

    SYM_RADD,
    SYM_RSUB,
    SYM_RMUL,
    SYM_RDIV,
    SYM_RMOD,
    SYM_RBAND,
    SYM_RBOR,
    SYM_RXOR,
    SYM_RLSHFT,
    SYM_RRSHFT,

    SYM_GET,
    SYM_SET,

    SYM_NEG,
    SYM_INV,

    SYM_EQ,
    SYM_LT,
    SYM_LE,
    SYM_GT,
    SYM_GE,

    SYM_POW,
    SYM_RPOW,

    SYM_END
} MethodSymbol;

// Struct that stores the info needed to
// jump to handler code and to restore the
// VM state when handling exceptions
typedef struct Handler {
    enum {
        HANDLER_ENSURE,
        HANDLER_EXCEPT,
    } type;            // The type of the handler block
    uint8_t* address;  // The address of handler code
    Value* savedSp;    // Saved stack state before the try block was enterd
} Handler;

// Stackframe of a function executing in
// the virtual machine
typedef struct Frame {
    uint8_t* ip;                     // Instruction pointer
    Value* stack;                    // Base of stack for current frame
    Obj* fn;                         // Function associated with the frame (ObjClosure or ObjNative)
    Handler handlers[MAX_HANDLERS];  // Exception handlers
    uint8_t handlerCount;            // Exception handlers count
} Frame;

// The J* VM. This struct stores all the
// state needed to execute J* code.
struct JStarVM {
    // Paths searched for import
    ObjList* importPaths;

    // Built in classes
    ObjClass* clsClass;
    ObjClass* objClass;
    ObjClass* strClass;
    ObjClass* boolClass;
    ObjClass* lstClass;
    ObjClass* numClass;
    ObjClass* funClass;
    ObjClass* modClass;
    ObjClass* nullClass;
    ObjClass* stClass;
    ObjClass* tupClass;
    ObjClass* excClass;
    ObjClass* tableClass;
    ObjClass* udataClass;

    // Script arguments
    ObjList* argv;

    // The empty tuple (singleton)
    ObjTuple* emptyTup;

    // Current VM compiler (if any)
    Compiler* currCompiler;

    // Cached method names needed at runtime
    ObjString* methodSyms[SYM_END];

    // Loaded modules
    HashTable modules;

    // Current module and core module
    ObjModule *module, *core;

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
    HashTable stringPool;

    // Linked list of all open upvalues
    ObjUpvalue* upvalues;

    // Callback function to report errors
    JStarErrorCB errorCallback;

    // If set, the VM will break the eval loop as soon as possible.
    // Can be set asynchronously by a signal handler
    volatile sig_atomic_t evalBreak;

    // Custom data associated with the VM
    void* customData;

    // ---- Memory management ----

    // Linked list of all allocated objects (used in
    // the sweep phase of GC to free unreached objects)
    Obj* objects;

    size_t allocated;  // Bytes currently allocated
    size_t nextGC;     // Bytes at which the next GC will be triggered
    int heapGrowRate;  // Rate at which the heap will grow after a GC

    // Stack used to recursevely reach all the fields of reached objects
    Obj** reachedStack;
    size_t reachedCapacity, reachedCount;
};

bool getValueField(JStarVM* vm, ObjString* name);
bool setValueField(JStarVM* vm, ObjString* name);

bool getValueSubscript(JStarVM* vm);
bool setValueSubscript(JStarVM* vm);

bool callValue(JStarVM* vm, Value callee, uint8_t argc);
bool invokeValue(JStarVM* vm, ObjString* name, uint8_t argc);

void reserveStack(JStarVM* vm, size_t needed);
void swapStackSlots(JStarVM* vm, int a, int b);

bool runEval(JStarVM* vm, int evalDepth);
bool unwindStack(JStarVM* vm, int depth);

inline void push(JStarVM* vm, Value v) {
    *vm->sp++ = v;
}

inline Value pop(JStarVM* vm) {
    return *--vm->sp;
}

inline Value peek(JStarVM* vm) {
    return vm->sp[-1];
}

inline Value peek2(JStarVM* vm) {
    return vm->sp[-2];
}

inline Value peekn(JStarVM* vm, int n) {
    return vm->sp[-(n + 1)];
}

inline ObjClass* getClass(JStarVM* vm, Value v) {
#ifdef JSTAR_NAN_TAGGING
    if(IS_OBJ(v)) return AS_OBJ(v)->cls;
    if(IS_NUM(v)) return vm->numClass;

    switch(BITS_TAG(v)) {
    case HANDLE_BITS:
    case NULL_BITS:
        return vm->nullClass;
    case FALSE_BITS:
    case TRUE_BITS:
        return vm->boolClass;
    default:
        UNREACHABLE();
        return NULL;
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
    default:
        UNREACHABLE();
        return NULL;
    }
#endif
}

inline bool isInstance(JStarVM* vm, Value i, ObjClass* cls) {
    for(ObjClass* c = getClass(vm, i); c != NULL; c = c->superCls) {
        if(c == cls) {
            return true;
        }
    }
    return false;
}

#endif
