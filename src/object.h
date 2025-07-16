#ifndef OBJECT_H
#define OBJECT_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "code.h"
#include "int_hashtable.h"
#include "jstar.h"
#include "jstar_limits.h"
#include "object_types.h"
#include "value.h"
#include "value_hashtable.h"

// Top level variables defined in each module
#define MOD_NAME "__name__"  // The module's name
#define MOD_PATH "__path__"  // The module's file path
#define MOD_THIS "__this__"  // A reference to the module itself

struct Frame;

/**
 * Object system of the J* language.
 *
 * Every object shares the base fields of the Obj struct, including it as the
 * first field in their declaration. This permits the casting of any pointer to
 * to Obj* and back, implementing a sort of manual inheritance.
 *
 * In addition to object definitions, this file defines macros for testing and
 * casting Obj* pointers, as well as very low-level functions for object manipulation.
 *
 * Note that casting macros do not perform any checking, thus an Obj* pointer
 * should be tested before casting.
 */

// -----------------------------------------------------------------------------
// OBJECT CASTING MACROS
// -----------------------------------------------------------------------------

#define IS_BOUND_METHOD(o) (IS_OBJ(o) && AS_OBJ(o)->type == OBJ_BOUND_METHOD)
#define IS_LIST(o)         (IS_OBJ(o) && AS_OBJ(o)->type == OBJ_LIST)
#define IS_STRING(o)       (IS_OBJ(o) && AS_OBJ(o)->type == OBJ_STRING)
#define IS_FUNC(o)         (IS_OBJ(o) && AS_OBJ(o)->type == OBJ_FUNCTION)
#define IS_NATIVE(o)       (IS_OBJ(o) && AS_OBJ(o)->type == OBJ_NATIVE)
#define IS_CLASS(o)        (IS_OBJ(o) && AS_OBJ(o)->type == OBJ_CLASS)
#define IS_INSTANCE(o)     (IS_OBJ(o) && AS_OBJ(o)->type == OBJ_INST)
#define IS_MODULE(o)       (IS_OBJ(o) && AS_OBJ(o)->type == OBJ_MODULE)
#define IS_CLOSURE(o)      (IS_OBJ(o) && AS_OBJ(o)->type == OBJ_CLOSURE)
#define IS_GENERATOR(o)    (IS_OBJ(o) && AS_OBJ(o)->type == OBJ_GENERATOR)
#define IS_TUPLE(o)        (IS_OBJ(o) && AS_OBJ(o)->type == OBJ_TUPLE)
#define IS_STACK_TRACE(o)  (IS_OBJ(o) && AS_OBJ(o)->type == OBJ_STACK_TRACE)
#define IS_TABLE(o)        (IS_OBJ(o) && AS_OBJ(o)->type == OBJ_TABLE)
#define IS_USERDATA(o)     (IS_OBJ(o) && AS_OBJ(o)->type == OBJ_USERDATA)

#define AS_BOUND_METHOD(o) ((ObjBoundMethod*)AS_OBJ(o))
#define AS_LIST(o)         ((ObjList*)AS_OBJ(o))
#define AS_STRING(o)       ((ObjString*)AS_OBJ(o))
#define AS_FUNC(o)         ((ObjFunction*)AS_OBJ(o))
#define AS_NATIVE(o)       ((ObjNative*)AS_OBJ(o))
#define AS_CLASS(o)        ((ObjClass*)AS_OBJ(o))
#define AS_INSTANCE(o)     ((ObjInstance*)AS_OBJ(o))
#define AS_MODULE(o)       ((ObjModule*)AS_OBJ(o))
#define AS_CLOSURE(o)      ((ObjClosure*)AS_OBJ(o))
#define AS_GENERATOR(o)    ((ObjGenerator*)AS_OBJ(o))
#define AS_TUPLE(o)        ((ObjTuple*)AS_OBJ(o))
#define AS_STACK_TRACE(o)  ((ObjStackTrace*)AS_OBJ(o))
#define AS_TABLE(o)        ((ObjTable*)AS_OBJ(o))
#define AS_USERDATA(o)     ((ObjUserdata*)AS_OBJ(o))

// -----------------------------------------------------------------------------
// OBJECT DEFINITONS
// -----------------------------------------------------------------------------

// Base class of all the Objects.
// Defines shared properties of all objects, such as the type and the class
// field, as well as fields used for garbage collection, such as the reached
// flag (used to test when an object is reachable, and thus not collectable)
// and the next pointer, that points to the next object in the global linked
// list of all allocated objects (set up by the allocator in gc.c).
struct Obj {
    ObjType type;          // The type of the object
    bool reached;          // Flag used to signal that an object is reachable during a GC
    struct ObjClass* cls;  // The class of the Object
    struct Obj* next;      // Next object in the linked list of all allocated objects
};

// A J* String. In J* Strings are immutable and can contain arbitrary
// bytes since we explicitly store the string's length instead of relying on
// NUL termination. Nevertheless, a NUL byte is appended for ease of use in
// the C api.
struct ObjString {
    Obj base;
    size_t length;  // Length of the string
    uint32_t hash;  // The string's hash (gets calculated once at allocation)
    bool interned;  // Whether the string is interned or not
    char* data;     // The actual data of the string (NUL terminated)
};

// A J* module. Modules are the runtime representation of a J* file.
struct ObjModule {
    Obj base;
    ObjString* name;           // Name of the module
    ObjString* path;           // The path to the module file
    IntHashTable globalNames;  // HashTable mapping from global name to global value array
    int globalsCount;          // Number of globals in the module
    int globalsCapacity;       // Capacity of the globals array
    Value* globals;            // Array of global values
    JStarNativeReg* registry;  // Natives registered in this module
};

// Fields shared by all function objects (ObjFunction/ObjNative)
struct Prototype {
    Obj base;
    bool vararg;        // Whether the function is a vararg one
    uint8_t argsCount;  // The arity of the function
    uint8_t defCount;   // Number of default args of the function (0 if none)
    Value* defaults;    // Array of default arguments (NULL if no defaults)
    ObjModule* module;  // The module of the function
    ObjString* name;    // The name of the function
};

// A compiled J* function
struct ObjFunction {
    Prototype proto;
    Code code;             // The actual code chunk containing bytecodes
    uint8_t upvalueCount;  // The number of upvalues the function closes over
    int stackUsage;
};

// A C function callable from J*
struct ObjNative {
    Prototype proto;
    JStarNative fn;  // The C function that gets called
};

// A J* class. Classes are first class objects in J*.
struct ObjClass {
    Obj base;
    ObjString* name;            // The name of the class
    struct ObjClass* superCls;  // Pointer to the parent class (or NULL)
    int fieldCount;             // Number of fields of the class
    IntHashTable fields;        // HashTable containing a mapping for the object's fields
    ValueHashTable methods;     // HashTable containing methods (ObjFunction/ObjNative)
};

// An instance of a user defined Class
struct ObjInstance {
    Obj base;
    size_t capacity;  // Size of the fields array
    Value* fields;    // Array of fields of the instance
};

// A J* List. Lists are mutable sequences of values.
struct ObjList {
    Obj base;
    size_t capacity;  // Size of the List (how much space is currently allocated)
    size_t size;      // How many objects are currently in the list
    Value* arr;       // List elements
};

// A J* Tuple. Tuples are immutable sequences of values.
struct ObjTuple {
    Obj base;
    size_t size;  // Number of elements of the tuple
    Value arr[];  // Tuple elements (flexible array)
};

struct TableEntry {
    Value key;  // The key of the entry
    Value val;  // The actual value
};

// A J* Table. Tables are hash tables that map keys to values.
struct ObjTable {
    Obj base;
    size_t capacityMask;  // The size of the entries array
    size_t numEntries;    // The number of entries in the Table (including tombstones)
    size_t size;          // The number of actual entries in the Table (i.e. excluding tombstones)
    TableEntry* entries;  // The actual array of entries
};

// A bound method. It contains a method with an associated target.
struct ObjBoundMethod {
    Obj base;
    Value receiver;  // The receiver to which the method is bound
    Obj* method;     // The actual method
};

// An upvalue is a variable captured from an outer scope by a closure.
// when a closure of a function is created, it instantiates an ObjUpvalue
// for all  variables used in the function but declared in an outer one,
// and stores the stack address of such variable in the addr field.
// When a variable that is also an upvalue gets out of scope, its Value
// is copied in the closed field, and the addr field is set to &closed.
// This way the variable can continue to be used even if the stack frame
// that originally stored it has benn popped.
struct ObjUpvalue {
    Obj base;
    Value* addr;              // The address of the upvalue
    Value closed;             // Stores the upvalue when closed
    struct ObjUpvalue* next;  // Pointer to the next open upvalue. NULL when closed
};

// A closure always wraps an ObjFunction and stores the flattened hierarchy of
// Upvalues that the function closes over.
struct ObjClosure {
    Obj base;
    ObjFunction* fn;         // The function
    uint8_t upvalueCount;    // The number of Upvalues the function closes over
    ObjUpvalue* upvalues[];  // the actual Upvalues
};

struct SavedHandler {
    int type;
    uint8_t* address;
    size_t spOffset;
};

struct SavedFrame {
    uint8_t* ip;
    size_t stackTop;
    uint8_t handlerCount;
    SavedHandler handlers[MAX_HANDLERS];
};

// A generator is a special iterator-like object that has the ability
// to suspend its execution via a `yield` expression. Each time it is
// called, execution resumes from the last evaluated yield or, in case
// it is the first time calling it, from the start of the function. On
// resume, the yield expression evaluates to the Value passed in by the
// caller, making it possible for generators to emulate (stackless)
// coroutines. All the state needed to support suspension and resume is
// stored here (see `SuspendedFrame` and `savedStack`)
struct ObjGenerator {
    Obj base;
    enum {
        GEN_STARTED,
        GEN_RUNNING,
        GEN_SUSPENDED,
        GEN_DONE,
    } state;
    ObjClosure* closure;
    Value lastYield;
    SavedFrame frame;    // Saved generator frame
    size_t stackSize;    // The size of the generator stack
    Value savedStack[];  // The saved stack of the generator function
};

struct FrameRecord {
    int line;
    ObjString* path;
    ObjString* moduleName;
    ObjString* funcName;
};

// Object that contains the dump of the stack's frames.
// Used for storing the trace of an unhandled exception
struct ObjStackTrace {
    Obj base;
    int lastTracedFrame;
    int recordCapacity;
    int recordSize;
    FrameRecord* records;
};

// Garbage collected user data
struct ObjUserdata {
    Obj base;
    void (*finalize)(void*);  // Custom function to finalize the userdatum
    size_t size;              // The size ot the userdatum
    uint8_t data[];           // The data
};

// -----------------------------------------------------------------------------
// OBJECT ALLOCATION FUNCTIONS
// -----------------------------------------------------------------------------

// These functions use `gcAlloc` to allocate memory and then initialize the object with the supplied
// arguments, as well as setting all the bookkeping information needed by the GC (see struct Obj)
ObjFunction* newFunction(JStarVM* vm, ObjModule* m, ObjString* name, uint8_t args, uint8_t defCount,
                         bool varg);
ObjNative* newNative(JStarVM* vm, ObjModule* m, ObjString* name, uint8_t args, uint8_t defCount,
                     bool varg, JStarNative fn);
ObjGenerator* newGenerator(JStarVM* vm, ObjClosure* closure, size_t stackSize);
ObjUserdata* newUserData(JStarVM* vm, size_t size, void (*finalize)(void*));
ObjClass* newClass(JStarVM* vm, ObjString* name, ObjClass* superCls);
ObjModule* newModule(JStarVM* vm, const char* path, ObjString* name);
ObjBoundMethod* newBoundMethod(JStarVM* vm, Value b, Obj* method);
ObjInstance* newInstance(JStarVM* vm, ObjClass* cls);
ObjClosure* newClosure(JStarVM* vm, ObjFunction* fn);
ObjUpvalue* newUpvalue(JStarVM* vm, Value* addr);
ObjList* newList(JStarVM* vm, size_t capacity);
ObjTuple* newTuple(JStarVM* vm, size_t size);
ObjStackTrace* newStackTrace(JStarVM* vm);
ObjTable* newTable(JStarVM* vm);

// Allocate an uninitialized string of size `length`
ObjString* allocateString(JStarVM* vm, size_t length);

// Copy a c-string of size `length`. The string is automatically interned
ObjString* copyString(JStarVM* vm, const char* str, size_t length);

// Release the object's memory. It uses gcAlloc internally to let the GC know
void freeObject(JStarVM* vm, Obj* o);

// -----------------------------------------------------------------------------
// OBJECT MANIPULATION FUNCTIONS
// -----------------------------------------------------------------------------

// ObjInstance functions
int instanceSetField(JStarVM* vm, ObjClass* cls, ObjInstance* inst, ObjString* key, Value val);
void instanceSetFieldAtOffset(JStarVM* vm, ObjInstance* inst, int offset, Value val);
bool instanceGetField(JStarVM* vm, ObjClass* cls, ObjInstance* inst, ObjString* key, Value* out);
bool instanceGetFieldAtOffset(ObjInstance* inst, int offset, Value* out);
int instanceGetFieldOffset(JStarVM* vm, ObjClass* cls, ObjInstance* inst, ObjString* key);

// ObjModule functions
int moduleSetGlobal(JStarVM* vm, ObjModule* mod, ObjString* key, Value val);
void moduleSetGlobalAtOffset(JStarVM* vm, ObjModule* mod, int offset, Value val);
bool moduleGetGlobal(JStarVM* vm, ObjModule* mod, ObjString* key, Value* out);
void moduleGetGlobalAtOffset(ObjModule* mod, int offset, Value* out);
int moduleGetGlobalOffset(JStarVM* vm, ObjModule* mod, ObjString* key);

// ObjList functions
void listAppend(JStarVM* vm, ObjList* lst, Value v);
void listInsert(JStarVM* vm, ObjList* lst, size_t index, Value val);
void listRemove(JStarVM* vm, ObjList* lst, size_t index);

// ObjString functions
uint32_t stringGetHash(ObjString* str);
bool stringEquals(ObjString* s1, ObjString* s2);

// ObjStacktrace functions
// Dumps a frame in an `ObjStackTrace`
void stacktraceDump(JStarVM* vm, ObjStackTrace* st, struct Frame* f, int depth);

// Misc functions
// Get the value array of a List or a Tuple
Value* getValues(Obj* obj, size_t* size);
// Get the prototype field of a Function object
Prototype* getPrototype(Obj* fn);
// Convert a JStarBuffer into an ObjString and zeroes `b`
ObjString* jsrBufferToString(JStarBuffer* b);

// -----------------------------------------------------------------------------
// DEBUG
// -----------------------------------------------------------------------------

#ifdef JSTAR_DBG_PRINT_GC
extern const char* ObjTypeNames[];
#endif

// Prints an Obj in a human readable form
void printObj(Obj* o);

#endif
