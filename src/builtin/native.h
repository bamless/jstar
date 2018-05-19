#ifndef NATIVE_H
#define NATIVE_H

#include <stdint.h>
#include <string.h>

#include "object.h"

#define NATIVE(name) Value name(VM *vm, Value *args)
#define IS_INT(v) (IS_NUM(v) && (int64_t)AS_NUM(v) == AS_NUM(v))
#define BL_THIS ((ObjInstance*) AS_OBJ(args[0]))

void blSetField(VM *vm, ObjInstance *o, const char *name, Value val);
bool blGetField(VM *vm, ObjInstance *o, const char *name, Value *ret);

void blSetGlobal(VM *vm, const char *fname, Value val);
bool blGetGlobal(VM *vm, const char *fname, Value *ret);

void blRuntimeError(VM *vm, const char* format, ...);

// Object allocation

#define ALLOC(vm, size) allocate(vm, NULL, 0, size)
#define FREEARRAY(vm, type, obj, count) \
		allocate(vm, obj, sizeof(type) * count, 0)

void *allocate(VM *vm, void *ptr, size_t oldsize, size_t size);

ObjNative *newNative(VM *vm, ObjModule *module, ObjString *name, uint8_t argsCount, Native fn);
ObjBoundMethod *newBoundMethod(VM *vm, ObjInstance *b, ObjFunction *method);
ObjFunction *newFunction(VM *vm, ObjModule *module, ObjString *name, uint8_t argsCount);
ObjClass *newClass(VM *vm, ObjString *name, ObjClass *superCls);
ObjString *newString(VM *vm, char *cstring, size_t size);
ObjInstance *newInstance(VM *vm, ObjClass *cls);
ObjModule *newModule(VM *vm, ObjString *name);
ObjList *newList(VM *vm, size_t startSize);

ObjString *copyString(VM *vm, const char *str, size_t length);
ObjString *newStringFromBuf(VM *vm, char *buf, size_t length);

void listAppend(VM *vm, ObjList *lst, Value v);
void listInsert(VM *vm, ObjList *lst, size_t index, Value val);
void listRemove(VM *vm, ObjList *lst, size_t index);

#endif
