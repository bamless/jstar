#include "core.h"
#include "modules.h"
#include "import.h"
#include "vm.h"

#include <stdio.h>
#include <stdarg.h>
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

static void defMethodDefaults(BlangVM *vm, ObjModule *m, ObjClass *cls, Native n, const char *name, uint8_t argc, uint8_t defc, ...) {
	ObjString *strName = copyString(vm, name, strlen(name), true);
	push(vm, OBJ_VAL(strName));

	ObjNative *native = newNative(vm, m, strName, argc, n, defc);

	va_list args;
	va_start(args, defc);
	for(size_t i = 0; i < defc; i++) {
		native->c.defaults[i] = va_arg(args, Value);
	}
	va_end(args);

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

		BL_RETURN_OBJ(copyString(vm, str, strlen(str), false));
	}

	static NATIVE(bl_Object_class) {
		BL_RETURN_OBJ(AS_OBJ(args[0])->cls);
	}

	static NATIVE(bl_Object_hash) {
		uint64_t x = hash64((uint64_t) AS_OBJ(args[0]));
		BL_RETURN_NUM((uint32_t) x);
	}
// Object

// class Class
	static NATIVE(bl_Class_getName) {
		BL_RETURN_OBJ(AS_CLASS(args[0])->name);
	}

	static NATIVE(bl_Class_string) {
		Obj *o = AS_OBJ(args[0]);

		char str[256];
		snprintf(str, 255, "<Class %s@%p>", ((ObjClass*)o)->name->data, (void*) o);
		BL_RETURN_OBJ(copyString(vm, str, strlen(str), false));
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
	defMethod(vm, core, vm->clsClass, &bl_Class_string,  "__string__", 0);

	blEvaluateModule(vm, "__core__", "__core__", readBuiltInModule("__core__"));

	vm->strClass   = AS_CLASS(getDefinedName(vm, core, "String"));
	vm->boolClass  = AS_CLASS(getDefinedName(vm, core, "Boolean"));
	vm->lstClass   = AS_CLASS(getDefinedName(vm, core, "List"));
	vm->numClass   = AS_CLASS(getDefinedName(vm, core, "Number"));
	vm->funClass   = AS_CLASS(getDefinedName(vm, core, "Function"));
	vm->modClass   = AS_CLASS(getDefinedName(vm, core, "Module"));
	vm->nullClass  = AS_CLASS(getDefinedName(vm, core, "Null"));
	vm->stClass    = AS_CLASS(getDefinedName(vm, core, "StackTrace"));
	vm->tupClass   = AS_CLASS(getDefinedName(vm, core, "Tuple"));
	vm->rangeClass = AS_CLASS(getDefinedName(vm, core, "range"));

	core->base.cls = vm->modClass;

	// Set constructor for instatiable primitive classes
	defMethodDefaults(vm, core, vm->lstClass, &bl_List_new, "new", 2, 2, NUM_VAL(0), NULL_VAL);
	defMethodDefaults(vm, core, vm->rangeClass, &bl_range_new, "new", 3, 2, NULL_VAL, NUM_VAL(1));

	// Patch up the class field of any string or function that was allocated
	// before the creation of their corresponding class object
	for(Obj *o = vm->objects; o != NULL; o = o->next) {
		if(o->type == OBJ_STRING) {
			o->cls = vm->strClass;
		} else if(o->type == OBJ_CLOSURE || o->type == OBJ_FUNCTION || o->type == OBJ_NATIVE) {
			o->cls = vm->funClass;
		}
	}
}

NATIVE(bl_int) {
	if(IS_NUM(args[1])) {
		BL_RETURN_NUM((int64_t)AS_NUM(args[1]));
	}
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

		BL_RETURN_NUM(n);
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

		BL_RETURN_NUM(n);
	}
	if(IS_NUM(args[1])) {
		BL_RETURN(args[1]);
	}

	BL_RAISE_EXCEPTION(vm, "InvalidArgException", "Argument must be a number or a string.");
}

NATIVE(bl_list) {
	if(!checkInt(vm, args[1], "n")) {
		return true;
	}

	double size = AS_NUM(args[1]);

	ObjList *l = newList(vm, size);
	for(; l->count < size; l->count++) {
		l->arr[l->count] = args[2];
	}

	BL_RETURN_OBJ(l);
}

NATIVE(bl_isInt) {
	if(!checkInt(vm, args[1], "n")) {
		return true;
	}
	double n = AS_NUM(args[1]);
	BL_RETURN(BOOL_VAL(trunc(n) == n));
}

NATIVE(bl_char) {
	if(!checkInt(vm, args[1], "num")) {
		return true;
	}
	char c = AS_NUM(args[1]);
	BL_RETURN_OBJ(copyString(vm, &c, 1, true));
}

NATIVE(bl_ascii) {
	if(!checkStr(vm, args[1], "arg")) {
		return true;
	}

	ObjString *arg = AS_STRING(args[1]);
	if(arg->length != 1) {
		BL_RAISE_EXCEPTION(vm, "InvalidArgException", "arg must be a String of length 1");
	}

	char c = arg->data[0];
	BL_RETURN_NUM((int) c);
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
	fwrite(s->data, 1, s->length, stream);

	BL_RETURN_NULL;
}

// class Number {
	NATIVE(bl_Number_string) {
		char str[24]; // enough for .*g with DBL_DIG
		snprintf(str, sizeof(str) - 1, "%.*g", DBL_DIG, AS_NUM(args[0]));
		BL_RETURN_OBJ(copyString(vm, str, strlen(str), false));
	}

	NATIVE(bl_Number_class) {
		BL_RETURN_OBJ(vm->numClass);
	}

	NATIVE(bl_Number_hash) {
		double num = AS_NUM(args[0]);
		if(num == 0) num = 0;
		union {
			double d;
			uint64_t r;
		} c = {.d = num};
		uint64_t n = hash64(c.r);
		BL_RETURN_NUM((double) (uint32_t) n);
	}
// } Number

// class Boolean {
	NATIVE(bl_Boolean_string) {
		BL_RETURN_OBJ(AS_BOOL(args[0]) ? copyString(vm, "true", 4, true)
		                               : copyString(vm, "false", 5, true));
	}

	NATIVE(bl_Boolean_class) {
		BL_RETURN_OBJ(vm->boolClass);
	}
// } Boolean

// class Null {
	NATIVE(bl_Null_string) {
		BL_RETURN_OBJ(copyString(vm, "null", 4, true));
	}

	NATIVE(bl_Null_class) {
		BL_RETURN_OBJ(vm->nullClass);
	}
// } Null

// class Function {
	NATIVE(bl_Function_string) {
		const char *funType = NULL;
		const char *funName = NULL;
		const char *modName = NULL;

		switch(OBJ_TYPE(args[0])) {
		case OBJ_CLOSURE:
			funType = "function";
			funName = AS_CLOSURE(args[0])->fn->c.name->data;
			modName = AS_CLOSURE(args[0])->fn->c.module->name->data;
			break;
		case OBJ_NATIVE:
			funType = "native";
			funName = AS_NATIVE(args[0])->c.name->data;
			modName = AS_NATIVE(args[0])->c.module->name->data;
			break;
		case OBJ_BOUND_METHOD: {
			ObjBoundMethod *m = AS_BOUND_METHOD(args[0]);
			funType = "bound method";
			funName = m->method->type == OBJ_CLOSURE ?
			          ((ObjClosure*) m->method)->fn->c.name->data :
			          ((ObjNative*) m->method)->c.name->data;
			modName = m->method->type == OBJ_CLOSURE ?
			          ((ObjClosure*) m->method)->fn->c.module->name->data :
			          ((ObjNative*) m->method)->c.module->name->data;
			break;
		}
		default: break;
		}

		char str[512] = {0};
		snprintf(str, sizeof(str) - 1, "<%s %s.%s@%p>", funType, modName, funName, AS_OBJ(args[0]));

		BL_RETURN_OBJ(copyString(vm, str, strlen(str), false));
	}
// } Function

// class Module {
	NATIVE(bl_Module_string) {
		char str[256];
		ObjModule *m = AS_MODULE(args[0]);
		snprintf(str, sizeof(str) - 1, "<module %s@%p>", m->name->data, m);
		BL_RETURN_OBJ(copyString(vm, str, strlen(str), false));
	}
// } Module

// class List {
	NATIVE(bl_List_new) {
		if(!checkInt(vm, args[1], "size")) {
			return true;
		}

		double count = AS_NUM(args[1]);

		if(count < 0) BL_RAISE_EXCEPTION(vm, "TypeException", "size must be >= 0");
		ObjList *lst = newList(vm, count < 16 ? 16 : count);

		size_t c = count;
		for(size_t i = 0; i < c; i++) {
			lst->arr[i] = args[2];
		}
		lst->count = c;

		BL_RETURN_OBJ(lst);
	}

	NATIVE(bl_List_add) {
		ObjList *l = AS_LIST(args[0]);
		listAppend(vm, l, args[1]);
		BL_RETURN_TRUE;
	}

	NATIVE(bl_List_insert) {
		ObjList *l = AS_LIST(args[0]);
		size_t index = checkIndex(vm, args[1], l->count, "i");
		if(index == SIZE_MAX) return true;

		listInsert(vm, l, index, args[2]);
		BL_RETURN_NULL;
	}

	NATIVE(bl_List_size) {
		BL_RETURN_NUM(AS_LIST(args[0])->count);
	}

	NATIVE(bl_List_removeAt) {
		ObjList *l = AS_LIST(args[0]);
		size_t index = checkIndex(vm, args[1], l->count, "i");
		if(index == SIZE_MAX) return true;

		Value r = l->arr[index];
		listRemove(vm, l, index);
		BL_RETURN(r);
	}

	NATIVE(bl_List_subList) {
		ObjList *list = AS_LIST(args[0]);

		size_t from = checkIndex(vm, args[1], list->count, "from");
		if(from == SIZE_MAX) return true;
		size_t to = checkIndex(vm, args[2], list->count + 1, "to");
		if(to == SIZE_MAX) return true;

		if(from >= to) {
			BL_RAISE_EXCEPTION(vm, "InvalidArgException", "from must be < to.");
		}

		size_t numElems = to - from;
		ObjList *subList = newList(vm, numElems < 16 ? 16 : numElems);

		memcpy(subList->arr, list->arr + from, numElems * sizeof(Value));
		subList->count = numElems;

		BL_RETURN_OBJ(subList);
	}

	NATIVE(bl_List_clear) {
		AS_LIST(args[0])->count = 0;
		BL_RETURN_NULL;
	}

	NATIVE(bl_List_iter) {
		ObjList *lst = AS_LIST(args[0]);

		if(IS_NULL(args[1])) {
			if(lst->count == 0) BL_RETURN_FALSE;
			BL_RETURN_NUM(0);
		}

		if(IS_NUM(args[1])) {
			double idx = AS_NUM(args[1]);
			if(idx >= 0 && idx < lst->count - 1) BL_RETURN_NUM(idx + 1);
		}

		BL_RETURN_FALSE;
	}

	NATIVE(bl_List_next) {
		ObjList *lst = AS_LIST(args[0]);

		if(IS_NUM(args[1])) {
			double idx = AS_NUM(args[1]);
			if(idx >= 0 && idx < lst->count) BL_RETURN(lst->arr[(size_t)idx]);
		}

		BL_RETURN_NULL;
	}
// } List

// class Tuple {
	NATIVE(bl_Tuple_size) {
		BL_RETURN_NUM(AS_TUPLE(args[0])->size);
	}

	NATIVE(bl_Tuple_iter) {
		ObjTuple *tup = AS_TUPLE(args[0]);

		if(IS_NULL(args[1])) {
			if(tup->size == 0) BL_RETURN_FALSE;
			BL_RETURN_NUM(0);
		}

		if(IS_NUM(args[1])) {
			double idx = AS_NUM(args[1]);
			if(idx >= 0 && idx < tup->size - 1) BL_RETURN_NUM(idx + 1);
		}

		BL_RETURN_FALSE;
	}

	NATIVE(bl_Tuple_next) {
		ObjTuple *tup = AS_TUPLE(args[0]);

		if(IS_NUM(args[1])) {
			double idx = AS_NUM(args[1]);
			if(idx >= 0 && idx < tup->size) BL_RETURN(tup->arr[(size_t)idx]);
		}

		BL_RETURN_NULL;
	}
// }

// class String {
	NATIVE(bl_substr) {
		ObjString *str = AS_STRING(args[0]);

		size_t from = checkIndex(vm, args[1], str->length, "from");
		if(from == SIZE_MAX) return true;
		size_t to = checkIndex(vm, args[2], str->length + 1, "to");
		if(to == SIZE_MAX) return true;

		if(from >= to) {
			BL_RAISE_EXCEPTION(vm, "InvalidArgException",
							"argument to must be >= from.");
		}

		size_t len = to - from;
		ObjString *sub = allocateString(vm, len);
		memcpy(sub->data, str->data + from, len);

		BL_RETURN_OBJ(sub);
	}

	NATIVE(bl_String_join) {
		if(!checkList(vm, args[1], "lst")) {
			return false;
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

		BL_RETURN_OBJ(joined);
	}

	NATIVE(bl_String_length) {
		BL_RETURN_NUM(AS_STRING(args[0])->length);
	}

	NATIVE(bl_String_hash) {
		BL_RETURN_NUM(stringGetHash(AS_STRING(args[0])));
	}

	NATIVE(bl_String_eq) {
		if(!IS_STRING(args[1])) {
			BL_RETURN_FALSE;
		}
		
		ObjString *s1 = AS_STRING(args[0]);
		ObjString *s2 = AS_STRING(args[1]);

		if(s1->interned && s2->interned) {
			BL_RETURN(s1 == s2 ? TRUE_VAL : FALSE_VAL);
		}

		if(s1->length != s2->length) BL_RETURN(FALSE_VAL);

		BL_RETURN(memcmp(s1->data, s2->data, s1->length) == 0 ? TRUE_VAL : FALSE_VAL);
	}

	NATIVE(bl_String_iter) {
		ObjString *s = AS_STRING(args[0]);

		if(IS_NULL(args[1])) {
			if(s->length == 0) BL_RETURN_FALSE;
			BL_RETURN_NUM(0);
		}

		if(IS_NUM(args[1])) {
			double idx = AS_NUM(args[1]);
			if(idx >= 0 && idx < s->length - 1) BL_RETURN_NUM(idx + 1);
		}

		BL_RETURN_FALSE;
	}

	NATIVE(bl_String_next) {
		ObjString *s = AS_STRING(args[0]);

		if(IS_NUM(args[1])) {
			double idx = AS_NUM(args[1]);
			if(idx >= 0 && idx < s->length) {
				BL_RETURN_OBJ(copyString(vm, s->data + (size_t)idx, 1, true));
			}
		}

		BL_RETURN_NULL;
	}
// } String

// class range {
	NATIVE(bl_range_new) {
		Value from, to, step;

		if(IS_NULL(args[2])) {
			from = NUM_VAL(0);
			to = args[1];
		} else {
			from = args[1];
			to = args[2];
		}
		
		step = args[3];

		if(!checkInt(vm, from, "from") || !checkInt(vm, to, "to") || !checkInt(vm, step, "step")) {
			return true;
		}

		BL_RETURN_OBJ(newRange(vm, AS_NUM(from), AS_NUM(to), AS_NUM(step)));
	}

	NATIVE(bl_range_iter) {
		ObjRange *r = AS_RANGE(args[0]);

		bool incr = r->step > 0;

		if(IS_NULL(args[1])) {
			if(incr ? r->start < r->stop : r->start > r->stop)
				BL_RETURN_NUM(r->start);
			else
				BL_RETURN_FALSE;
		}

		if(IS_NUM(args[1])) {
			double i = AS_NUM(args[1]) + r->step;
			
			if(incr) {
				if(i < r->stop) BL_RETURN_NUM(i);
			} else {
				if(i > r->stop) BL_RETURN_NUM(i);
			}
		}

		BL_RETURN_FALSE;
	}

	NATIVE(bl_range_next) {
		if(IS_NUM(args[1])) {
			BL_RETURN(args[1]);
		}
		BL_RETURN_NULL;
	}
// }
