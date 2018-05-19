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

	ObjNative *native = newNative(vm, m, strName, argc, n);

	pop(vm);

	hashTablePut(&cls->methods, strName, OBJ_VAL(native));
}

static uint64_t hash64(uint64_t x) {
	x = (x ^ (x >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
	x = (x ^ (x >> 27)) * UINT64_C(0x94d049bb133111eb);
	x = x ^ (x >> 31);
	return x;
}

// class Object methods
	static NATIVE(bl_Object_string) {
		Obj *o = AS_OBJ(args[0]);

		char str[256];
		snprintf(str, 255, "<%s@%p>", o->cls->name->data, (void*) o);

		return OBJ_VAL(copyString(vm, str, strlen(str)));
	}

	static NATIVE(bl_Object_class) {
		return OBJ_VAL(AS_OBJ(args[0])->cls);
	}

	static NATIVE(bl_Object_equals) {
		return BOOL_VAL(valueEquals(args[0], args[1]));
	}

	static NATIVE(bl_Object_hash) {
		uint64_t x = hash64((uint64_t) AS_OBJ(args[0]));
		return NUM_VAL((uint32_t) x);
	}
// Object

// class Class methods
	static NATIVE(bl_Class_getName) {
		return OBJ_VAL(AS_CLASS(args[0])->name);
	}
// Class

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
	defMethod(vm, core, vm->objClass, &bl_Object_string, "__string__", 0);
	defMethod(vm, core, vm->objClass, &bl_Object_class,  "__class__", 0);
	defMethod(vm, core, vm->objClass, &bl_Object_hash,   "__hash__", 0);
	defMethod(vm, core, vm->objClass, &bl_Object_equals, "__equals__", 1);

	// Patch up Class object infotmation
	vm->clsClass->superCls = vm->objClass;
	hashTableMerge(&vm->clsClass->methods, &vm->objClass->methods);
	defMethod(vm, core, vm->clsClass, &bl_Class_getName, "getName", 0);

	evaluateModule(vm, "__core__", readBuiltInModule("__core__"));

	vm->strClass  = AS_CLASS(getDefinedName(vm, core, "String"));
	vm->boolClass = AS_CLASS(getDefinedName(vm, core, "Boolean"));
	vm->lstClass  = AS_CLASS(getDefinedName(vm, core, "List"));
	vm->numClass  = AS_CLASS(getDefinedName(vm, core, "Number"));
	vm->funClass  = AS_CLASS(getDefinedName(vm, core, "Function"));
	vm->modClass  = AS_CLASS(getDefinedName(vm, core, "Module"));
	vm->nullClass = AS_CLASS(getDefinedName(vm, core, "Null"));

	core->base.cls = vm->modClass;

	for(Obj *o = vm->objects; o != NULL; o = o->next) {
		if(o->type == OBJ_STRING) {
			o->cls = vm->strClass;
		} else if(o->type == OBJ_FUNCTION || o->type == OBJ_NATIVE) {
			o->cls = vm->funClass;
		}
	}
}

NATIVE(bl_list) {
	if(!IS_INT(args[1]) || AS_NUM(args[1]) < 0) {
		blRuntimeError(vm, "Argument 1 of list(n, init) must be a positive integer.");
		return NULL_VAL;
	}

	ObjList *l = newList(vm, AS_NUM(args[1]));
	for(size_t i = 0; i < l->size; i++) {
		l->arr[i] = args[2];
	}
	l->count = l->size;

	return OBJ_VAL(l);
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

NATIVE(bl_printstr) {
	printf("%s\n", AS_STRING(args[1])->data);
	return NULL_VAL;
}

// class Number {
	NATIVE(bl_Number_string) {
		char str[24];
		snprintf(str, sizeof(str) - 1, "%.*g", __DBL_DIG__, AS_NUM(args[0]));
		return OBJ_VAL(copyString(vm, str, strlen(str)));
	}

	NATIVE(bl_Number_class) {
		return OBJ_VAL(vm->numClass);
	}

	NATIVE(bl_Number_hash) {
		union {
			double d;
			uint64_t r;
		} c = {.d = AS_NUM(args[0])};
		uint64_t n = hash64(c.r);
		return NUM_VAL((uint32_t) n);
	}
// } Number

// class Boolean {
	NATIVE(bl_Boolean_string) {
		return AS_BOOL(args[0]) ? OBJ_VAL(copyString(vm, "true", 4))
		                        : OBJ_VAL(copyString(vm, "false", 5));
	}

	NATIVE(bl_Boolean_class) {
		return OBJ_VAL(vm->boolClass);
	}
// } Boolean

// class Null {
	NATIVE(bl_Null_string) {
		return OBJ_VAL(copyString(vm, "null", 4));
	}

	NATIVE(bl_Null_class) {
		return OBJ_VAL(vm->nullClass);
	}
// } Null

// class Function {
	NATIVE(bl_Function_string) {
		const char *funType = NULL;
		const char *funName = NULL;
		switch(OBJ_TYPE(args[0])) {
		case OBJ_FUNCTION:
			funType = "function";
			funName = AS_FUNC(args[0])->name->data;
			break;
		case OBJ_NATIVE:
			funType = "native";
			funName = AS_NATIVE(args[0])->name->data;
			break;
		case OBJ_BOUND_METHOD: {
			ObjBoundMethod *m = AS_BOUND_METHOD(args[0]);
			funType = "bound method";
			funName = m->method->base.type == OBJ_FUNCTION ?
			          ((ObjFunction*) m->method)->name->data :
			          ((ObjNative*) m->method)->name->data;
			break;
		}
		default: break;
		}

		char str[256];
		snprintf(str, sizeof(str) - 1, "<%s %s@%p>", funType, funName, AS_OBJ(args[0]));

		return OBJ_VAL(copyString(vm, str, strlen(str)));
	}
// } Function

// class Module {
	NATIVE(bl_Module_string) {
		char str[256];
		ObjModule *m = AS_MODULE(args[0]);
		snprintf(str, sizeof(str) - 1, "<module %s@%p>", m->name->data, m);
		return OBJ_VAL(copyString(vm, str, strlen(str)));
	}
// } Module

// class List {
	NATIVE(bl_List_add) {
		ObjList *l = AS_LIST(args[0]);
		listAppend(vm, l, args[1]);

		return TRUE_VAL;
	}

	NATIVE(bl_List_insert) {
		if(!IS_INT(args[1])) {
			blRuntimeError(vm, "Argument 1 of insertt(i, e) must be an integer.");
			return NULL_VAL;
		}

		ObjList *l = AS_LIST(args[0]);

		double index = AS_NUM(args[1]);
		if(index < 0 || index > l->count) {
			blRuntimeError(vm, "List index out of bound: %d.", (int) index);
			return NULL_VAL;
		}

		listInsert(vm, l, index, args[2]);

		return TRUE_VAL;
	}

	NATIVE(bl_List_length) {
		return NUM_VAL(AS_LIST(args[0])->count);
	}

	NATIVE(bl_List_remove) {
		if(!IS_INT(args[1])) {
			blRuntimeError(vm, "Argument of remove(i) must be an integer.");
			return NULL_VAL;
		}

		listRemove(vm, AS_LIST(args[0]), AS_NUM(args[1]));
		return NULL_VAL;
	}

	NATIVE(bl_List_clear) {
		AS_LIST(args[0])->count = 0;
		return NULL_VAL;
	}
// } List

// class String {
	NATIVE(bl_String_length) {
		return NUM_VAL(AS_STRING(args[0])->length);
	}

	NATIVE(bl_String_hash) {
		return NUM_VAL(AS_STRING(args[0])->hash);
	}
// } String
