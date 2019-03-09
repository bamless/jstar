#ifndef OBJECT_H
#define OBJECT_H

#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>

#include "hashtable.h"
#include "chunk.h"
#include "value.h"

#include "util/enum.h"

/**
 * Object system of the Blang language.
 * Every object shares the base fields of the struct Obj, including it as the
 * first field in their declaration. This permits the casting of any pinter to
 * an object to Obj* and back, implementing a sort of manual polymorphism.
 *
 * Object should be tested by using the IS_* macro before casting them with the
 * corresponding AS_* macro, since this last one doesn't perform any checking,
**/

typedef struct BlangVM BlangVM;

#ifdef DBG_PRINT_GC
DECLARE_TO_STRING(ObjType);
#endif

// Macros for testing and casting objects
#define OBJ_TYPE(o) (AS_OBJ(o)->type)

#define IS_BOUND_METHOD(o) (IS_OBJ(o) && OBJ_TYPE(o) == OBJ_BOUND_METHOD)
#define IS_LIST(o)         (IS_OBJ(o) && OBJ_TYPE(o) == OBJ_LIST)
#define IS_STRING(o)       (IS_OBJ(o) && OBJ_TYPE(o) == OBJ_STRING)
#define IS_FUNC(o)         (IS_OBJ(o) && OBJ_TYPE(o) == OBJ_FUNCTION)
#define IS_NATIVE(o)       (IS_OBJ(o) && OBJ_TYPE(o) == OBJ_NATIVE)
#define IS_CLASS(o)        (IS_OBJ(o) && OBJ_TYPE(o) == OBJ_CLASS)
#define IS_INSTANCE(o)     (IS_OBJ(o) && OBJ_TYPE(o) == OBJ_INST)
#define IS_MODULE(o)       (IS_OBJ(o) && OBJ_TYPE(o) == OBJ_MODULE)

#define AS_BOUND_METHOD(o) ((ObjBoundMethod*) AS_OBJ(o))
#define AS_LIST(o)         ((ObjList*)        AS_OBJ(o))
#define AS_STRING(o)       ((ObjString*)      AS_OBJ(o))
#define AS_FUNC(o)         ((ObjFunction*)    AS_OBJ(o))
#define AS_NATIVE(o)       ((ObjNative*)      AS_OBJ(o))
#define AS_CLASS(o)        ((ObjClass*)       AS_OBJ(o))
#define AS_INSTANCE(o)     ((ObjInstance*)    AS_OBJ(o))
#define AS_MODULE(o)       ((ObjModule*)      AS_OBJ(o))

// Type of objects. These types are used internally by the object system and are
// Never exposed to the user (unless when using the c interface)
#define OBJTYPE(X) \
	X(OBJ_STRING) X(OBJ_NATIVE) X(OBJ_FUNCTION) X(OBJ_CLASS) X(OBJ_INST) \
	X(OBJ_MODULE) X(OBJ_LIST) X(OBJ_BOUND_METHOD)

DEFINE_ENUM(ObjType, OBJTYPE);

typedef struct ObjClass ObjClass;

/**
 * Base class of all the Object system.
 * Defines shared properties of all objects, such as the type and the class
 * field as well as fields used for the garbage collection, such as the reached
 * flag (used to test when an object is reachable, and thus not collectable)
 * and the next pointer, that points to the next object in the global linked
 * list of all allocated objects (set up by the allocator in memory.c).
 */
typedef struct Obj {
	ObjType type;         // The type of the object
	bool reached;         // Flag used during garbage collection
	struct ObjClass *cls; // The class of the Object
	struct Obj *next;     // Next object in the linked list of all allocated objects
} Obj;

// Note: In blang all strings are interned. This means that 2 strings with
// Equal content will always refer to the same address. This add some cost
// during allocation, since a look-up in the global string hash table is needed,
// but it saves a lot of time in comparisons, since only the address should be
// tested
typedef struct ObjString {
	Obj base;
	size_t length; // Length of the string
	char *data;    // The actual data of the string (NUL terminated)
	uint32_t hash; // The string's hash (gets calculated once at allocation)
} ObjString;

typedef struct ObjModule {
	Obj base;
	ObjString *name;   // Name of the module
	HashTable globals; // HashTable containing the global variables of the module
} ObjModule;

typedef struct ObjFunction {
	Obj base;
	uint8_t argsCount; // The arity of the function
	Chunk chunk;       // The actual code chunk containing bytecodes
	Value *defaults;   // default arguments
	uint8_t defaultc;  // number of default arguments
	ObjString *name;   // The name of the function
	ObjModule *module; // The module to which the function belongs
} ObjFunction;

// Native function signature
typedef bool (*Native)(BlangVM *vm, Value *argv, Value *ret);

typedef struct ObjNative {
	Obj base;
	uint8_t argsCount; // The arity of the native
	Native fn;         // The c native function that gets called
	Value *defaults;   // default arguments
	uint8_t defaultc;  // number of default arguments
	ObjString *name;   // The name of the native
	ObjModule *module; // The module to which the native belongs
} ObjNative;

typedef struct ObjClass {
	Obj base;
	ObjString *name;           // The name of the class
	struct ObjClass *superCls; // Pointer t the parent class (or NULL)
	HashTable methods;         // HashTable containing methods (ObjFunction)
} ObjClass;

typedef struct ObjInstance {
	Obj base;
	HashTable fields; // HashTable containing the fields of the instance
} ObjInstance;

typedef struct ObjList {
	Obj base;
	size_t size;  // Size of the List (how much space is currently allocated)
	size_t count; // How many objects are currently in the list
	Value *arr;
} ObjList;

typedef struct ObjBoundMethod {
	Obj base;
	Value bound; // The value to which the method is bound
	Obj *method; // The actual method
} ObjBoundMethod;

void printObj(Obj *o);

#endif
