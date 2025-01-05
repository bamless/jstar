#ifndef OBJECT_TYPES_H
#define OBJECT_TYPES_H

/**
 * This file contains forward declarations of all the object types used in the J* VM.
 * See "object.h" for the actual object definitions.
 */

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

typedef struct Obj Obj;

typedef struct ObjString ObjString;

typedef struct ObjModule ObjModule;

typedef struct Prototype Prototype;

typedef struct ObjFunction ObjFunction;

typedef struct ObjNative ObjNative;

typedef struct ObjClass ObjClass;

typedef struct ObjInstance ObjInstance;

typedef struct ObjList ObjList;

typedef struct ObjTuple ObjTuple;

typedef struct TableEntry TableEntry;
typedef struct ObjTable ObjTable;

typedef struct ObjBoundMethod ObjBoundMethod;

typedef struct ObjUpvalue ObjUpvalue;

typedef struct ObjClosure ObjClosure;

typedef struct SavedHandler SavedHandler;
typedef struct SavedFrame SavedFrame;
typedef struct ObjGenerator ObjGenerator;

typedef struct FrameRecord FrameRecord;
typedef struct ObjStackTrace ObjStackTrace;

typedef struct ObjUserdata ObjUserdata;

#endif
