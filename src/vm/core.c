#include "core.h"
#include "modules.h"
#include "import.h"
#include "vm.h"

#include <stdio.h>

static ObjClass* createClass(VM *vm, ObjModule *m, ObjClass *sup, const char *name) {
	ObjString *n = copyString(vm, name, strlen(name));
	push(vm, OBJ_VAL(n));

	ObjClass *c = newClass(vm, n, sup);

	pop(vm);

	hashTablePut(&m->globals, n, OBJ_VAL(c));

	return c;
}

static Value getDefinedName(VM *vm, ObjModule *m, const char *name) {
	Value v = NULL_VAL;
	hashTableGet(&m->globals, copyString(vm, name, strlen(name)), &v);
	return v;
}

static void defMethod(VM *vm, ObjModule *m, ObjClass *cls, Native n, const char *name, uint8_t argc) {
	ObjString *strName = copyString(vm, name, strlen(name));
	push(vm, OBJ_VAL(strName));

	ObjNative *native = newNative(vm, m, argc, n);
	native->name = strName;

	pop(vm);

	hashTablePut(&cls->methods, strName, OBJ_VAL(native));
}

// class Object methods

static NATIVE(bl_Object_toString) {
	Obj *o = AS_OBJ(args[0]);

	char str[256];
	snprintf(str, 255, "%s@%p", o->cls->name->data, (void*) o);

	return OBJ_VAL(copyString(vm, str, strlen(str)));
}

static NATIVE(bl_Object_getClass) {
	return OBJ_VAL(AS_OBJ(args[0])->cls);
}

// class Class methods

static NATIVE(bl_Class_getName) {
	return OBJ_VAL(AS_CLASS(args[0])->name);
}

void initCoreLibrary(VM *vm) {
	ObjString *name = copyString(vm, "__core__", 8);

	push(vm, OBJ_VAL(name));

	ObjModule *core = newModule(vm, name);
	setModule(vm, core->name, core);

	pop(vm);

	// Setup the class object. It will be the class of every other class
	vm->clsClass = createClass(vm, core, NULL, "Class");
	vm->clsClass->base.cls = vm->clsClass; // Class is the class of itself

	// Setup the base class of the object hierarchy
	vm->objClass = createClass(vm, core, NULL, "Object"); // Object has no superclass
	defMethod(vm, core, vm->objClass, &bl_Object_toString, "toString", 0);
	defMethod(vm, core, vm->objClass, &bl_Object_getClass, "getClass", 0);

	// Patch up Class object infotmation
	vm->clsClass->superCls = vm->objClass;
	hashTableMerge(&vm->clsClass->methods, &vm->objClass->methods);
	defMethod(vm, core, vm->clsClass, &bl_Class_getName, "getName", 0);

	evaluateModule(vm, "__core__", readBuiltInModule("__core__"));

	vm->strClass  = AS_CLASS(getDefinedName(vm, core, "String"));
	vm->boolClass = AS_CLASS(getDefinedName(vm, core, "Boolean"));
	vm->numClass  = AS_CLASS(getDefinedName(vm, core, "Number"));
	vm->funClass  = AS_CLASS(getDefinedName(vm, core, "Function"));
	vm->modClass  = AS_CLASS(getDefinedName(vm, core, "Module"));

	core->base.cls = vm->modClass;

	for(Obj *o = vm->objects; o != NULL; o = o->next) {
		if(o->type == OBJ_STRING) {
			o->cls = vm->strClass;
		} else if(o->type == OBJ_FUNCTION || o->type == OBJ_NATIVE) {
			o->cls = vm->funClass;
		}
	}
}

NATIVE(bl_error) {
	blRuntimeError(vm, AS_STRING(args[1])->data);
	return NULL_VAL;
}

NATIVE(bl_isInt) {
	if(IS_NUM(args[1])) {
		double n = AS_NUM(args[1]);
		return BOOL_VAL((int64_t) n == n);
	}
	return FALSE_VAL;
}

NATIVE(bl_str) {
	if(IS_NUM(args[1])) {
		char str[256];
		snprintf(str, 255, "%.*g", __DBL_DIG__, AS_NUM(args[1]));
		return OBJ_VAL(copyString(vm, str, strlen(str)));
	} else if(IS_BOOL(args[1])) {
		const char *str = AS_BOOL(args[1]) ? "true" : "false";
		return OBJ_VAL(copyString(vm, str, strlen(str)));
	} else if(IS_NULL(args[1])) {
		return OBJ_VAL(copyString(vm, "null", 4));
	} else if(IS_STRING(args[1])) {
		return args[1];
	} else {
		return bl_Object_toString(vm, 0, args + 1);
	}
}
