#ifndef OBJECT_H
#define OBJECT_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "code.h"
#include "field_index.h"
#include "hash_table.h"
#include "jstar.h"
#include "jstar_limits.h"
#include "value.h"

// Top level variables defined in each module
#define MOD_NAME "__name__"  // The module's name
#define MOD_PATH "__path__"  // The module's file path
#define MOD_THIS "__this__"  // A reference to the module itself

struct Frame;

#ifdef JSTAR_DBG_PRINT_GC
extern const char* ObjTypeNames[];
#endif

/**
 * Object system of the J* language.
 * Every object shares the base fields of the Obj struct, including it as the
 * first field in their declaration. This permits the casting of any pointer to
 * to Obj* and back, implementing a sort of manual inheritance.
 *
 * In addition to object definitions, this file defines macros for testing and
 * casting Obj* pointers.
 * Note that casting macros do not perform any checking, thus an Obj* pointer
 * should be tested before casting.
 */

// -----------------------------------------------------------------------------
// OBJECT MACROS
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

// Object type.
// These types are used internally by the object system and are never
// exposed to the user, to whom all values behave like class instances.
// The enum is defined using X-macros in order to automatically generate
// string names of enum constats (see ObjTypeNames array in object.c)
#define OBJTYPE(X)      \
    X(OBJ_STRING)       \
    X(OBJ_NATIVE)       \
    X(OBJ_FUNCTION)     \
    X(OBJ_CLASS)        \
    X(OBJ_INST)         \
    X(OBJ_MODULE)       \
    X(OBJ_LIST)         \
    X(OBJ_BOUND_METHOD) \
    X(OBJ_STACK_TRACE)  \
    X(OBJ_CLOSURE)      \
    X(OBJ_GENERATOR)    \
    X(OBJ_UPVALUE)      \
    X(OBJ_TUPLE)        \
    X(OBJ_TABLE)        \
    X(OBJ_USERDATA)

typedef enum ObjType {
#define ENUM_ELEM(elem) elem,
    OBJTYPE(ENUM_ELEM)
#undef ENUM_ELEM
} ObjType;

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
typedef struct ObjModule {
    Obj base;
    ObjString* name;           // Name of the module
    ObjString* path;           // The path to the module file
    FieldIndex globalNames;    // HashTable mapping from global name to global value array
    int globalsCount;          // Number of globals in the module
    int globalsCapacity;       // Capacity of the globals array
    Value* globals;            // Array of global values
    JStarNativeReg* registry;  // Natives registered in this module
} ObjModule;

// Fields shared by all function objects (ObjFunction/ObjNative)
typedef struct {
    Obj base;
    bool vararg;        // Whether the function is a vararg one
    uint8_t argsCount;  // The arity of the function
    uint8_t defCount;   // Number of default args of the function (0 if none)
    Value* defaults;    // Array of default arguments (NULL if no defaults)
    ObjModule* module;  // The module of the function
    ObjString* name;    // The name of the function
} Prototype;

// A compiled J* function
typedef struct ObjFunction {
    Prototype proto;
    Code code;             // The actual code chunk containing bytecodes
    uint8_t upvalueCount;  // The number of upvalues the function closes over
    int stackUsage;
} ObjFunction;

// A C function callable from J*
typedef struct ObjNative {
    Prototype proto;
    JStarNative fn;  // The C function that gets called
} ObjNative;

// A J* class. Classes are first class objects in J*.
typedef struct ObjClass {
    Obj base;
    ObjString* name;            // The name of the class
    struct ObjClass* superCls;  // Pointer to the parent class (or NULL)
    int fieldCount;             // Number of fields of the class
    FieldIndex fields;          // HashTable containing a mapping for the object's fields
    HashTable methods;          // HashTable containing methods (ObjFunction/ObjNative)
} ObjClass;

// An instance of a user defined Class
typedef struct ObjInstance {
    Obj base;
    size_t capacity;  // Size of the fields array
    Value* fields;    // Array of fields of the instance
} ObjInstance;

// A J* List. Lists are mutable sequences of values.
typedef struct ObjList {
    Obj base;
    size_t capacity;  // Size of the List (how much space is currently allocated)
    size_t size;      // How many objects are currently in the list
    Value* arr;       // List elements
} ObjList;

// A J* Tuple. Tuples are immutable sequences of values.
typedef struct ObjTuple {
    Obj base;
    size_t size;  // Number of elements of the tuple
    Value arr[];  // Tuple elements (flexible array)
} ObjTuple;

typedef struct {
    Value key;  // The key of the entry
    Value val;  // The actual value
} TableEntry;

// A J* Table. Tables are hash tables that map keys to values.
typedef struct ObjTable {
    Obj base;
    size_t capacityMask;  // The size of the entries array
    size_t numEntries;    // The number of entries in the Table (including tombstones)
    size_t size;          // The number of actual entries in the Table (i.e. excluding tombstones)
    TableEntry* entries;  // The actual array of entries
} ObjTable;

// A bound method. It contains a method with an associated target.
typedef struct ObjBoundMethod {
    Obj base;
    Value receiver;  // The receiver to which the method is bound
    Obj* method;     // The actual method
} ObjBoundMethod;

// An upvalue is a variable captured from an outer scope by a closure.
// when a closure of a function is created, it instantiates an ObjUpvalue
// for all  variables used in the function but declared in an outer one,
// and stores the stack address of such variable in the addr field.
// When a variable that is also an upvalue gets out of scope, its Value
// is copied in the closed field, and the addr field is set to &closed.
// This way the variable can continue to be used even if the stack frame
// that originally stored it has benn popped.
typedef struct ObjUpvalue {
    Obj base;
    Value* addr;              // The address of the upvalue
    Value closed;             // Stores the upvalue when closed
    struct ObjUpvalue* next;  // Pointer to the next open upvalue. NULL when closed
} ObjUpvalue;

// A closure always wraps an ObjFunction and stores the flattened hierarchy of
// Upvalues that the function closes over.
typedef struct ObjClosure {
    Obj base;
    ObjFunction* fn;         // The function
    uint8_t upvalueCount;    // The number of Upvalues the function closes over
    ObjUpvalue* upvalues[];  // the actual Upvalues
} ObjClosure;

typedef struct {
    int type;
    uint8_t* address;
    size_t spOffset;
} SavedHandler;

typedef struct {
    uint8_t* ip;
    size_t stackTop;
    uint8_t handlerCount;
    SavedHandler handlers[MAX_HANDLERS];
} SupsendedFrame;

// A generator is a special iterator-like object that has the ability
// to suspend its execution via a `yield` expression. Each time it is
// called, execution resumes from the last evaluated yield or, in case
// it is the first time calling it, from the start of the function. On
// resume, the yield expression evaluates to the Value passed in by the
// caller, making it possible for generators to emulate (stackless)
// coroutines. All the state needed to support suspension and resume is
// stored here (see `SuspendedFrame` and `savedStack`)
typedef struct ObjGenerator {
    Obj base;
    enum {
        GEN_STARTED,
        GEN_RUNNING,
        GEN_SUSPENDED,
        GEN_DONE,
    } state;
    ObjClosure* closure;
    Value lastYield;
    SupsendedFrame frame;  // Saved generator frame
    size_t stackSize;      // The size of the generator stack
    Value savedStack[];    // The saved stack of the generator function
} ObjGenerator;

typedef struct {
    int line;
    ObjString* moduleName;
    ObjString* funcName;
} FrameRecord;

// Object that contains the dump of the stack's frames.
// Used for storing the trace of an unhandled exception
typedef struct ObjStackTrace {
    Obj base;
    int lastTracedFrame;
    int recordCapacity;
    int recordSize;
    FrameRecord* records;
} ObjStackTrace;

// Garbage collected user data
typedef struct ObjUserdata {
    Obj base;
    void (*finalize)(void*);  // Custom function to finalize the userdatum
    size_t size;              // The size ot the userdatum
    uint8_t data[];           // The data
} ObjUserdata;

// -----------------------------------------------------------------------------
// OBJECT ALLOCATION FUNCTIONS
// -----------------------------------------------------------------------------

// These functions use gcAlloc to acquire memory and then initialize
// the object with the supplied arguments, as well as setting all the
// bookkeping information needed by the GC (see struct Obj)
ObjFunction* newFunction(JStarVM* vm, ObjModule* m, uint8_t args, uint8_t defCount, bool varg);
ObjNative* newNative(JStarVM* vm, ObjModule* m, uint8_t args, uint8_t defCount, bool varg);
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
int setField(JStarVM* vm, ObjClass* cls, ObjInstance* inst, ObjString* key, Value val);
void setFieldAtOffset(JStarVM* vm, ObjInstance* inst, int offset, Value val);
bool getField(JStarVM* vm, ObjClass* cls, ObjInstance* inst, ObjString* key, Value* val);
bool getFieldAtOffset(ObjInstance* inst, int offset, Value* val);
int getFieldIdx(JStarVM* vm, ObjClass* cls, ObjInstance* inst, ObjString* key);

// ObjModule functions
int setGlobal(JStarVM* vm, ObjModule* mod, ObjString* key, Value val);
void setGlobalAtOffset(JStarVM* vm, ObjModule* mod, int offset, Value val);
bool getGlobal(JStarVM* vm, ObjModule* mod, ObjString* key, Value* val);
bool getGlobalAtOffset(ObjModule* mod, int offset, Value* val);
int getGlobalIdx(JStarVM* vm, ObjModule* mod, ObjString* key);

// ObjList functions
void listAppend(JStarVM* vm, ObjList* lst, Value v);
void listInsert(JStarVM* vm, ObjList* lst, size_t index, Value val);
void listRemove(JStarVM* vm, ObjList* lst, size_t index);

// ObjString functions
uint32_t stringGetHash(ObjString* str);
bool stringEquals(ObjString* s1, ObjString* s2);

// ObjStacktrace functions
// Dumps a frame in a ObjStackTrace
void stacktraceDump(JStarVM* vm, ObjStackTrace* st, struct Frame* f, int depth);

// Get the value array of a List or a Tuple
Value* getValues(Obj* obj, size_t* size);

// Get the prototype field of a Function object
Prototype* getPrototype(Obj* fn);

// Convert a JStarBuffer to an ObjString
ObjString* jsrBufferToString(JStarBuffer* b);

// Wraps arbitrary data in a JStarBuffer. Used for adapting arbitrary bytes to be used in
// API functions that expect a JStarBuffer, without copying them first.
JStarBuffer jsrBufferWrap(struct JStarVM* vm, const void* data, size_t len);

// -----------------------------------------------------------------------------
// DEBUG FUNCTIONS
// -----------------------------------------------------------------------------

// Prints an Obj in a human readable form
void printObj(Obj* o);

#endif  // OBJECT_H
