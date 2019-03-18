#include "core.h"
#include "modules.h"
#include "import.h"
#include "vm.h"

#include <stdio.h>
#include <string.h>
#include <math.h>
#include <limits.h>
#include <errno.h>
#include <float.h>

static ObjClass* createClass(BlangVM *vm, ObjModule *m, ObjClass *sup, const char *name) {
	ObjString *n = copyString(vm, name, strlen(name), true);
	push(vm, OBJ_VAL(n));

	ObjClass *c = newClass(vm, n, sup);

	pop(vm);

	hashTablePut(&m->globals, n, OBJ_VAL(c));

	return c;
}

static Value getDefinedName(BlangVM *vm, ObjModule *m, const char *name) {
	Value v = NULL_VAL;
	hashTableGet(&m->globals, copyString(vm, name, strlen(name), true), &v);
	return v;
}

static void defMethod(BlangVM *vm, ObjModule *m, ObjClass *cls, Native n, const char *name, uint8_t argc) {
	ObjString *strName = copyString(vm, name, strlen(name), true);
	push(vm, OBJ_VAL(strName));

	ObjNative *native = newNative(vm, m, strName, argc, n, 0);

	pop(vm);

	hashTablePut(&cls->methods, strName, OBJ_VAL(native));
}

static uint64_t hash64(uint64_t x) {
	x = (x ^ (x >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
	x = (x ^ (x >> 27)) * UINT64_C(0x94d049bb133111eb);
	x = x ^ (x >> 31);
	return x;
}

// class Object
	static NATIVE(bl_Object_string) {
		Obj *o = AS_OBJ(args[0]);

		char str[256];
		snprintf(str, 255, "<%s@%p>", o->cls->name->data, (void*) o);

		BL_RETURN(OBJ_VAL(copyString(vm, str, strlen(str), false)));
	}

	static NATIVE(bl_Object_class) {
		BL_RETURN(OBJ_VAL(AS_OBJ(args[0])->cls));
	}

	static NATIVE(bl_Object_hash) {
		uint64_t x = hash64((uint64_t) AS_OBJ(args[0]));
		BL_RETURN(NUM_VAL((uint32_t) x));
	}
// Object

// class Class
	static NATIVE(bl_Class_getName) {
		BL_RETURN(OBJ_VAL(AS_CLASS(args[0])->name));
	}
// Class

void initCoreLibrary(BlangVM *vm) {
	ObjString *name = copyString(vm, "__core__", 8, true);

	push(vm, OBJ_VAL(name));

	ObjModule *core = newModule(vm, name);
	setModule(vm, core->name, core);
	vm->core = core;

	pop(vm);

	// Setup the class object. It will be the class of every other class
	vm->clsClass = createClass(vm, core, NULL, "Class");
	vm->clsClass->base.cls = vm->clsClass; // Class is the class of itself

	// Setup the base class of the object hierarchy
	vm->objClass = createClass(vm, core, NULL, "Object"); // Object has no superclass
	defMethod(vm, core, vm->objClass, &bl_Object_string, "__string__", 0);
	defMethod(vm, core, vm->objClass, &bl_Object_class,  "__class__", 0);
	defMethod(vm, core, vm->objClass, &bl_Object_hash,   "__hash__", 0);

	// Patch up Class object information
	vm->clsClass->superCls = vm->objClass;
	hashTableMerge(&vm->clsClass->methods, &vm->objClass->methods);
	defMethod(vm, core, vm->clsClass, &bl_Class_getName, "getName", 0);

	blEvaluateModule(vm, "__core__", "__core__", readBuiltInModule("__core__"));

	vm->strClass  = AS_CLASS(getDefinedName(vm, core, "String"));
	vm->boolClass = AS_CLASS(getDefinedName(vm, core, "Boolean"));
	vm->lstClass  = AS_CLASS(getDefinedName(vm, core, "List"));
	vm->numClass  = AS_CLASS(getDefinedName(vm, core, "Number"));
	vm->funClass  = AS_CLASS(getDefinedName(vm, core, "Function"));
	vm->modClass  = AS_CLASS(getDefinedName(vm, core, "Module"));
	vm->nullClass = AS_CLASS(getDefinedName(vm, core, "Null"));
	vm->excClass  = AS_CLASS(getDefinedName(vm, core, "Exception"));
	vm->stClass   = AS_CLASS(getDefinedName(vm, core, "StackTrace"));

	core->base.cls = vm->modClass;

	// Patch up the class field of any string or function that was allocated
	// before the creation of their corresponding class object
	for(Obj *o = vm->objects; o != NULL; o = o->next) {
		if(o->type == OBJ_STRING) {
			o->cls = vm->strClass;
		} else if(o->type == OBJ_FUNCTION || o->type == OBJ_NATIVE) {
			o->cls = vm->funClass;
		}
	}
}

NATIVE(bl_int) {
	if(IS_STRING(args[1])) {
		char *end = NULL;
		char *nstr = AS_STRING(args[1])->data;
		long long n = strtoll(nstr, &end, 10);

		if((n == 0 && end == nstr) || *end != '\0') {
			BL_RAISE_EXCEPTION(vm, "InvalidArgException", "\"%s\".", nstr);
		}
		if(n == LLONG_MAX) {
			BL_RAISE_EXCEPTION(vm, "InvalidArgException", "Overflow: \"%s\".", nstr);
		}
		if(n == LLONG_MIN) {
			BL_RAISE_EXCEPTION(vm, "InvalidArgException", "Underflow: \"%s\".", nstr);
		}

		BL_RETURN(NUM_VAL(n));
	}
	if(IS_NUM(args[1])) {
		BL_RETURN(NUM_VAL((int64_t)AS_NUM(args[1])));
	}

	BL_RAISE_EXCEPTION(vm, "InvalidArgException",
			"Argument must be a number or a string.");
}

NATIVE(bl_num) {
	if(IS_STRING(args[1])) {
		errno = 0;

		char *end = NULL;
		char *nstr = AS_STRING(args[1])->data;
		double n = strtod(nstr, &end);

		if((n == 0 && end == nstr) || *end != '\0') {
			BL_RAISE_EXCEPTION(vm, "InvalidArgException", "\"%s\".", nstr);
		}
		if(n == HUGE_VAL || n == -HUGE_VAL) {
			BL_RAISE_EXCEPTION(vm,
				"InvalidArgException", "Overflow: \"%s\".", nstr);
		}
		if(n == 0 && errno == ERANGE) {
			BL_RAISE_EXCEPTION(vm,
				"InvalidArgException", "Underflow: \"%s\".", nstr);
		}

		BL_RETURN(NUM_VAL(n));
	}
	if(IS_NUM(args[1])) {
		BL_RETURN(args[1]);
	}

	BL_RAISE_EXCEPTION(vm, "InvalidArgException", "Argument must be a number or a string.");
}

NATIVE(bl_list) {
	if(!IS_INT(args[1]) || AS_NUM(args[1]) < 0) {
		BL_RAISE_EXCEPTION(vm, "InvalidArgException",
				"Argument 1 of list(n, init) must be a positive integer.");
	}

	ObjList *l = newList(vm, AS_NUM(args[1]));
	for(size_t i = 0; i < l->size; i++) {
		l->arr[i] = args[2];
	}
	l->count = l->size;

	BL_RETURN(OBJ_VAL(l));
}

NATIVE(bl_isInt) {
	if(IS_NUM(args[1])) {
		double n = AS_NUM(args[1]);
		BL_RETURN(BOOL_VAL((int64_t) n == n));
	}
	BL_RETURN(FALSE_VAL);
}

NATIVE(bl_char) {
	if(!IS_INT(args[1])) {
		BL_RAISE_EXCEPTION(vm, "InvalidArgException", "num must be an integer");
	}

	char c = AS_NUM(args[1]);
	BL_RETURN(OBJ_VAL(copyString(vm, &c, 1, true)));
}

NATIVE(bl_ascii) {
	if(!IS_STRING(args[1]) || AS_STRING(args[1])->length != 1) {
		BL_RAISE_EXCEPTION(vm, "InvalidArgException", "arg must be a string of length 1");
	}

	char c = AS_STRING(args[1])->data[0];
	BL_RETURN(NUM_VAL((int) c));
}

NATIVE(bl_printstr) {
	FILE *stream = stdout;

	if(!IS_NULL(args[2])) {
		if(!IS_INSTANCE(args[2])) {
			BL_RAISE_EXCEPTION(vm, "TypeException", "stream is not a file");
		}

		ObjInstance *file = AS_INSTANCE(args[2]);

		Value h, closed;
		bool fail = false;

		fail |= !blGetField(vm, file, "_closed", &closed) || !IS_BOOL(closed);
		fail |= !blGetField(vm, file, "_handle", &h) || !IS_HANDLE(h);

		if(fail) {
			BL_RAISE_EXCEPTION(vm, "TypeException", "stream is not a file");
		}

		if(AS_BOOL(closed)) {
			BL_RAISE_EXCEPTION(vm, "IOException", "closed file");
		}

		stream = AS_HANDLE(h);
	}

	ObjString *s = AS_STRING(args[1]);
	if(fwrite(s->data, 1, s->length, stream) == 0) {
		BL_RAISE_EXCEPTION(vm, "IOException", NULL);
	}

	BL_RETURN(NULL_VAL);
}

// class Number {
	NATIVE(bl_Number_string) {
		char str[24];
		snprintf(str, sizeof(str) - 1, "%.*g", DBL_DIG, AS_NUM(args[0]));
		BL_RETURN(OBJ_VAL(copyString(vm, str, strlen(str), false)));
	}

	NATIVE(bl_Number_class) {
		BL_RETURN(OBJ_VAL(vm->numClass));
	}

	NATIVE(bl_Number_hash) {
		double num = AS_NUM(args[0]);
		if(num == 0) num = 0;
		union {
			double d;
			uint64_t r;
		} c = {.d = num};
		uint64_t n = hash64(c.r);
		BL_RETURN(NUM_VAL((uint32_t) n));
	}
// } Number

// class Boolean {
	NATIVE(bl_Boolean_string) {
		BL_RETURN(AS_BOOL(args[0]) ? OBJ_VAL(copyString(vm, "true", 4, true))
		                        : OBJ_VAL(copyString(vm, "false", 5, true)));
	}

	NATIVE(bl_Boolean_class) {
		BL_RETURN(OBJ_VAL(vm->boolClass));
	}
// } Boolean

// class Null {
	NATIVE(bl_Null_string) {
		BL_RETURN(OBJ_VAL(copyString(vm, "null", 4, true)));
	}

	NATIVE(bl_Null_class) {
		BL_RETURN(OBJ_VAL(vm->nullClass));
	}
// } Null

// class Function {
	NATIVE(bl_Function_string) {
		const char *funType = NULL;
		const char *funName = NULL;
		const char *modName = NULL;

		switch(OBJ_TYPE(args[0])) {
		case OBJ_FUNCTION:
			funType = "function";
			funName = AS_FUNC(args[0])->name->data;
			modName = AS_FUNC(args[0])->module->name->data;
			break;
		case OBJ_NATIVE:
			funType = "native";
			funName = AS_NATIVE(args[0])->name->data;
			modName = AS_NATIVE(args[0])->module->name->data;
			break;
		case OBJ_BOUND_METHOD: {
			ObjBoundMethod *m = AS_BOUND_METHOD(args[0]);
			funType = "bound method";
			funName = m->method->type == OBJ_FUNCTION ?
			          ((ObjFunction*) m->method)->name->data :
			          ((ObjNative*) m->method)->name->data;
			funName = m->method->type == OBJ_FUNCTION ?
			          ((ObjFunction*) m->method)->module->name->data :
			          ((ObjNative*) m->method)->module->name->data;
			break;
		}
		default: break;
		}

		char str[512] = {0};
		snprintf(str, sizeof(str) - 1, "<%s %s.%s@%p>", funType, modName, funName, AS_OBJ(args[0]));

		BL_RETURN(OBJ_VAL(copyString(vm, str, strlen(str), false)));
	}
// } Function

// class Module {
	NATIVE(bl_Module_string) {
		char str[256];
		ObjModule *m = AS_MODULE(args[0]);
		snprintf(str, sizeof(str) - 1, "<module %s@%p>", m->name->data, m);
		BL_RETURN(OBJ_VAL(copyString(vm, str, strlen(str), false)));
	}
// } Module

// class List {
	NATIVE(bl_List_add) {
		ObjList *l = AS_LIST(args[0]);
		listAppend(vm, l, args[1]);
		BL_RETURN(TRUE_VAL);
	}

	NATIVE(bl_List_insert) {
		if(!IS_INT(args[1])) {
			BL_RAISE_EXCEPTION(vm, "InvalidArgException",
			 		"Argument 1 of insert() must be an integer.");
			BL_RETURN(NULL_VAL);
		}

		ObjList *l = AS_LIST(args[0]);
		double index = AS_NUM(args[1]);
		if(index < 0 || index > l->count) {
			BL_RAISE_EXCEPTION(vm, "IndexOutOfBoundException",
					"List index out of bound: %d.", (int)index);
			BL_RETURN(NULL_VAL);
		}

		listInsert(vm, l, index, args[2]);
		BL_RETURN(TRUE_VAL);
	}

	NATIVE(bl_List_size) {
		BL_RETURN(NUM_VAL(AS_LIST(args[0])->count));
	}

	NATIVE(bl_List_removeAt) {
		if(!IS_INT(args[1])) {
			BL_RAISE_EXCEPTION(vm, "InvalidArgException",
			 		"Argument of removeAt() must be an integer.");
			BL_RETURN(NULL_VAL);
		}

		ObjList *l = AS_LIST(args[0]);
		double index = AS_NUM(args[1]);
		if(index < 0 || index > l->count) {
			BL_RAISE_EXCEPTION(vm, "IndexOutOfBoundException",
						"List index out of bound: %d.", (int)index);
			BL_RETURN(NULL_VAL);
		}

		listRemove(vm, l, index);
		BL_RETURN(NULL_VAL);
	}

	NATIVE(bl_List_clear) {
		AS_LIST(args[0])->count = 0;
		BL_RETURN(NULL_VAL);
	}
// } List

// class String {
	NATIVE(bl_substr) {
		if(!IS_INT(args[1]) || !IS_INT(args[2])) {
			BL_RAISE_EXCEPTION(vm, "InvalidArgException",
						"arguments of substr() must be integers.");
			BL_RETURN(NULL_VAL);
		}

		ObjString *str = AS_STRING(args[0]);
		int64_t from = AS_NUM(args[1]);
		int64_t to = AS_NUM(args[2]);

		if(from > to) {
			BL_RAISE_EXCEPTION(vm, "InvalidArgException",
							"argument to must be >= from.");
		}
		if(from < 0 || (size_t)from > str->length - 1) {
			BL_RAISE_EXCEPTION(vm, "IndexOutOfBoundException",
					"String index out of bounds: from %d.", from);
		}
		if((size_t)to > str->length) {
			BL_RAISE_EXCEPTION(vm, "IndexOutOfBoundException",
					"String index out of bounds: to %d.", from);
		}

		size_t len = to - from;
		ObjString *sub = allocateString(vm, len);
		memcpy(sub->data, str->data + from, len);

		BL_RETURN(OBJ_VAL(sub));
	}

	NATIVE(bl_String_join) {
		if(!IS_LIST(args[1])) {
			BL_RAISE_EXCEPTION(vm, "TypeException", "Argument to join must be a List");
		}

		ObjString *sep = AS_STRING(args[0]);
		ObjList *strings = AS_LIST(args[1]);

		size_t length = 0;
		for(size_t i = 0; i < strings->count; i++) {
			if(!IS_STRING(strings->arr[i]))
				BL_RAISE_EXCEPTION(vm, "TypeException", "All elements in iterable must be strings.");
			
			length += AS_STRING(strings->arr[i])->length;
			if(strings->count > 1 && i != strings->count - 1)
				length += sep->length;
		}

		ObjString *joined = allocateString(vm, length);

		length = 0;
		for(size_t i = 0; i < strings->count; i++) {
			ObjString *str = AS_STRING(strings->arr[i]);

			memcpy(joined->data + length, str->data, str->length);
			length += str->length;

			if(strings->count > 1 && i != strings->count - 1) {
				memcpy(joined->data + length, sep->data, sep->length);
				length += sep->length;
			}
		}

		BL_RETURN(OBJ_VAL(joined));
	}

	NATIVE(bl_String_length) {
		BL_RETURN(NUM_VAL(AS_STRING(args[0])->length));
	}

	NATIVE(bl_String_hash) {
		BL_RETURN(NUM_VAL(stringGetHash(AS_STRING(args[0]))));
	}

	NATIVE(bl_String_eq) {
		if(!IS_STRING(args[1])) {
			BL_RETURN(FALSE_VAL);
		}
		ObjString *s1 = AS_STRING(args[0]);
		ObjString *s2 = AS_STRING(args[1]);

		if(s1->interned && s2->interned) {
			BL_RETURN(s1 == s2 ? TRUE_VAL : FALSE_VAL);
		}
		BL_RETURN(strcmp(s1->data, s2->data) == 0 ? TRUE_VAL : FALSE_VAL);
	}
// } String
