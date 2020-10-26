#ifndef VM_H
#define VM_H

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include "compiler.h"
#include "const.h"
#include "disassemble.h"
#include "hashtable.h"
#include "jstar.h"
#include "object.h"
#include "opcode.h"
#include "signal.h"
#include "util.h"
#include "value.h"

// This stores the info needed to jump
// to handler code and to restore the
// VM state when handling exceptions
typedef struct Handler {
#define HANDLER_ENSURE OP_SETUP_ENSURE
#define HANDLER_EXCEPT OP_SETUP_EXCEPT
    uint8_t type;      // The type of the handler block (HANDLER_ENSURE/HANDLER_EXCEPT)
    uint8_t* address;  // The address of the handler code
    Value* savesp;     // Stack pointer to restore before executing the code at `address`
} Handler;

// Stackframe of a function executing in
// the virtual machine
typedef struct Frame {
    uint8_t* ip;                    // Instruction pointer
    Value* stack;                   // Base of stack for current frame
    Obj* fn;                        // Function associated with the frame (ObjClosure or ObjNative)
    Handler handlers[HANDLER_MAX];  // Exception handlers
    uint8_t handlerc;               // Exception handlers count
} Frame;

// Enum representing the various overloadable
// operators of the language
typedef enum Overload {
    // Binary overloads
    ADD_OVERLOAD,
    SUB_OVERLOAD,
    MUL_OVERLOAD,
    DIV_OVERLOAD,
    MOD_OVERLOAD,
    // Reverse binary overloads
    RADD_OVERLOAD,
    RSUB_OVERLOAD,
    RMUL_OVERLOAD,
    RDIV_OVERLOAD,
    RMOD_OVERLOAD,
    // Subscript overloads
    GET_OVERLOAD,
    SET_OVERLOAD,
    // Comparison and ordering overloads
    EQ_OVERLOAD,
    LT_OVERLOAD,
    LE_OVERLOAD,
    GT_OVERLOAD,
    GE_OVERLOAD,
    NEG_OVERLOAD,
    // Sentinel
    OVERLOAD_SENTINEL
} Overload;

// The J* VM. This struct stores all the
// state needed to execute J* code.
struct JStarVM {
    // Paths searched for import
    ObjList* importpaths;

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

    // Current VM compiler (if any)
    Compiler* currCompiler;

    // Constant strings needed by compiler and runtime
    ObjString *ctor, *stacktrace, *excError, *next, *iter;
    ObjString* overloads[OVERLOAD_SENTINEL];

    // The empty tuple (singleton)
    ObjTuple* emptyTup;

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

    // Stack used during native function calls
    Value* apiStack;

    // Constant string pool, for interned strings
    HashTable strings;

    // Linked list of all open upvalues
    ObjUpvalue* upvalues;

    // Callback function to report errors
    JStarErrorCB errorCallback;

    // If set, the VM will break the eval loop as soon as possible.
    // Can be set asynchronously by a signal handler
    volatile sig_atomic_t evalBreak;

    // ---- Memory management ----

    // Linked list of all allocated objects (used in
    // the sweep phase of GC to free unreached objects)
    Obj* objects;

    bool disableGC;    // Whether the garbage collector is enabled or disabled
    size_t allocated;  // Bytes currently allocated
    size_t nextGC;     // Bytes at which the next GC will be triggered
    int heapGrowRate;  // Rate at which the heap will grow after a GC

    // Stack used to recursevely reach all the fields of reached objects
    Obj** reachedStack;
    size_t reachedCapacity, reachedCount;
};

bool runEval(JStarVM* vm, int evalDepth);
void ensureStack(JStarVM* vm, size_t needed);

bool getFieldFromValue(JStarVM* vm, ObjString* name);
bool setFieldOfValue(JStarVM* vm, ObjString* name);

bool callValue(JStarVM* vm, Value callee, uint8_t argc);
bool invokeValue(JStarVM* vm, ObjString* name, uint8_t argc);

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
    if(IS_NUM(v)) return vm->numClass;
    if(IS_OBJ(v)) return AS_OBJ(v)->cls;

    switch(GET_TAG(v)) {
    case TRUE_TAG:
    case FALSE_TAG:
        return vm->boolClass;
    case NULL_TAG:
    default:
        return vm->nullClass;
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
    default:
        return vm->nullClass;
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

inline int apiStackIndex(JStarVM* vm, int slot) {
    ASSERT(vm->sp - slot > vm->apiStack, "API stack slot would be negative");
    ASSERT(vm->apiStack + slot < vm->sp, "API stack overflow");
    if(slot < 0) return vm->sp + slot - vm->apiStack;
    return slot;
}

// Get the value at API stack slot "slot"
inline Value apiStackSlot(JStarVM* vm, int slot) {
    ASSERT(vm->sp - slot > vm->apiStack, "API stack slot would be negative");
    ASSERT(vm->apiStack + slot < vm->sp, "API stack overflow");
    if(slot < 0) return vm->sp[slot];
    return vm->apiStack[slot];
}

#endif
