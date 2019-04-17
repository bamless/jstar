#include "core.h"
#include "modules.h"
#include "import.h"
#include "memory.h"
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
		Obj *o = AS_OBJ(vm->apiStack[0]);

		char str[256];
		snprintf(str, 255, "<%s@%p>", o->cls->name->data, (void*) o);

		blPushString(vm, str);
		return true;
	}

	static NATIVE(bl_Object_class) {
		push(vm, OBJ_VAL(AS_OBJ(vm->apiStack[0])->cls));
		return true;
	}

	static NATIVE(bl_Object_hash) {
		uint64_t x = hash64((uint64_t) AS_OBJ(vm->apiStack[0]));
		blPushNumber(vm, (uint32_t)x);
		return true;
	}
// Object

// class Class
	static NATIVE(bl_Class_getName) {
		push(vm, OBJ_VAL(AS_CLASS(vm->apiStack[0])->name));
		return true;
	}

	static NATIVE(bl_Class_string) {
		Obj *o = AS_OBJ(vm->apiStack[0]);

		char str[256];
		snprintf(str, 255, "<Class %s@%p>", ((ObjClass*)o)->name->data, (void*) o);

		blPushString(vm, str);
		return true;
	}
// Class

void initCoreLibrary(BlangVM *vm) {
	ObjString *name = copyString(vm, CORE_MODULE, strlen(CORE_MODULE), true);

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
	if(blIsNumber(vm, 1)) {
		blPushNumber(vm, trunc(blGetNumber(vm, 1)));
		return true;
	}
	if(blIsString(vm, 1)) {
		char *end = NULL;
		const char *nstr = blGetString(vm, 1);
		long long n = strtoll(nstr, &end, 10);

		if((n == 0 && end == nstr) || *end != '\0') {
			BL_RAISE(vm, "InvalidArgException", "\"%s\".", nstr);
		}
		if(n == LLONG_MAX) {
			BL_RAISE(vm, "InvalidArgException", "Overflow: \"%s\".", nstr);
		}
		if(n == LLONG_MIN) {
			BL_RAISE(vm, "InvalidArgException", "Underflow: \"%s\".", nstr);
		}

		blPushNumber(vm, n);
		return true;
	}

	BL_RAISE(vm, "InvalidArgException", "Argument must be a number or a string.");
}

NATIVE(bl_num) {
	if(blIsNumber(vm, 1)) {
		blPushNumber(vm, blGetNumber(vm, 1));
		return true;
	}
	if(blIsString(vm, 1)) {
		errno = 0;

		char *end = NULL;
		const char *nstr = blGetString(vm, 1);
		double n = strtod(nstr, &end);

		if((n == 0 && end == nstr) || *end != '\0') {
			BL_RAISE(vm, "InvalidArgException", "\"%s\".", nstr);
		}
		if(n == HUGE_VAL || n == -HUGE_VAL) {
			BL_RAISE(vm, "InvalidArgException", "Overflow: \"%s\".", nstr);
		}
		if(n == 0 && errno == ERANGE) {
			BL_RAISE(vm, "InvalidArgException", "Underflow: \"%s\".", nstr);
		}

		blPushNumber(vm, n);
		return true;
	}

	BL_RAISE(vm, "InvalidArgException", "Argument must be a number or a string.");
}

NATIVE(bl_isInt) {
	if(!blCheckInt(vm, 1, "n")) return false;
	blPushBoolean(vm, blIsInteger(vm, 1));
	return true;
}

NATIVE(bl_char) {
	if(!blCheckInt(vm, 1, "num")) return false;
	char c = blGetNumber(vm, 1);
	blPushStringSz(vm, &c, 1);
	return true;
}

NATIVE(bl_ascii) {
	if(!blCheckStr(vm, 1, "arg")) return false;

	const char *str = blGetString(vm, 1);
	if(strlen(str) != 1) BL_RAISE(vm, "InvalidArgException", "arg must be a String of length 1");

	char c = str[0];
	blPushNumber(vm, (double)c);
	return true;
}

NATIVE(bl_printstr) {
	FILE *stream = stdout;

	if(!blIsNull(vm, 2)) {
		if(!blCheckInstance(vm, 2, "stream")) return false;
	
		if(!blGetField(vm, 2, "_closed")) return false;
		if(!blGetField(vm, 2, "_handle")) return false;

		if(blGetBoolean(vm, -2)) BL_RAISE(vm, "IOException", "closed file");
		
		if(!blCheckHandle(vm, -1, "_handle")) return false;
		stream = blGetHandle(vm, -1);
	}

	const char *s = blGetString(vm, 1);
	fwrite(s, 1, blGetStringSz(vm, 1), stream);

	blPushNull(vm);
	return true;
}

NATIVE(bl_eval) {
	if(!blCheckStr(vm, 1, "source")) return false;
	if(vm->frameCount < 1) BL_RAISE(vm, "Exception", "eval() can only be called by another function");

	Frame *prevFrame = &vm->frames[vm->frameCount - 2];
	ObjModule *mod = prevFrame->fn.type == OBJ_CLOSURE ? 
	                 prevFrame->fn.closure->fn->c.module : 
					 prevFrame->fn.native->c.module;

	EvalResult res = blEvaluateModule(vm, "<string>", mod->name->data, blGetString(vm, 1));

	blPushBoolean(vm, res == VM_EVAL_SUCCSESS);
	return true;
}

// class Number {
	NATIVE(bl_Number_string) {
		char str[24]; // enough for .*g with DBL_DIG
		snprintf(str, sizeof(str) - 1, "%.*g", DBL_DIG, blGetNumber(vm, 0));
		blPushString(vm, str);
		return true;
	}

	NATIVE(bl_Number_class) {
		push(vm, OBJ_VAL(vm->numClass));
		return true;
	}

	NATIVE(bl_Number_hash) {
		double num = blGetNumber(vm, 0);
		if(num == 0) num = 0;
		union {
			double d;
			uint64_t r;
		} c = {.d = num};
		uint64_t n = hash64(c.r);
		blPushNumber(vm, (uint32_t)n);
		return true;
	}
// } Number

// class Boolean {
	NATIVE(bl_Boolean_string) {
		if(blGetBoolean(vm, 0)) blPushString(vm, "true");
		else blPushString(vm, "false");
		return true;
	}

	NATIVE(bl_Boolean_class) {
		push(vm, OBJ_VAL(vm->boolClass));
		return true;
	}
// } Boolean

// class Null {
	NATIVE(bl_Null_string) {
		blPushString(vm, "null");
		return true;
	}

	NATIVE(bl_Null_class) {
		push(vm, OBJ_VAL(vm->nullClass));
		return true;
	}
// } Null

// class Function {
	NATIVE(bl_Function_string) {
		const char *funType = NULL;
		const char *funName = NULL;
		const char *modName = NULL;

		switch(OBJ_TYPE(vm->apiStack[0])) {
		case OBJ_CLOSURE:
			funType = "function";
			funName = AS_CLOSURE(vm->apiStack[0])->fn->c.name->data;
			modName = AS_CLOSURE(vm->apiStack[0])->fn->c.module->name->data;
			break;
		case OBJ_NATIVE:
			funType = "native";
			funName = AS_NATIVE(vm->apiStack[0])->c.name->data;
			modName = AS_NATIVE(vm->apiStack[0])->c.module->name->data;
			break;
		case OBJ_BOUND_METHOD: {
			ObjBoundMethod *m = AS_BOUND_METHOD(vm->apiStack[0]);
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
		snprintf(str, sizeof(str) - 1, "<%s %s.%s@%p>", funType, modName, funName, AS_OBJ(vm->apiStack[0]));

		blPushString(vm, str);
		return true;
	}
// } Function

// class Module {
	NATIVE(bl_Module_string) {
		char str[256];
		ObjModule *m = AS_MODULE(vm->apiStack[0]);
		snprintf(str, sizeof(str) - 1, "<module %s@%p>", m->name->data, m);
		blPushString(vm, str);
		return true;
	}
// } Module

// class List {
	NATIVE(bl_List_new) {
		if(!blCheckInt(vm, 1, "size")) return false;
		double count = blGetNumber(vm, 1);

		if(count < 0) BL_RAISE(vm, "TypeException", "size must be >= 0");
		ObjList *lst = newList(vm, count < 16 ? 16 : count);
		lst->count = count;
		push(vm, OBJ_VAL(lst));

		if(IS_CLOSURE(vm->apiStack[2]) || IS_NATIVE(vm->apiStack[2])) {
			for(size_t i = 0; i < lst->count; i++) {
				blPushValue(vm, 2);
				blPushNumber(vm, i);
				if(blCall(vm, 1) != VM_EVAL_SUCCSESS) return false;
				lst->arr[i] = pop(vm);
			}
		} else {
			for(size_t i = 0; i < lst->count; i++) {
				lst->arr[i] = vm->apiStack[2];
			}
		}

		return true;
	}

	NATIVE(bl_List_add) {
		ObjList *l = AS_LIST(vm->apiStack[0]);
		listAppend(vm, l, vm->apiStack[1]);
		blPushNull(vm);
		return true;
	}

	NATIVE(bl_List_insert) {
		ObjList *l = AS_LIST(vm->apiStack[0]);
		size_t index = blCheckIndex(vm, 1, l->count, "i");
		if(index == SIZE_MAX) return false;

		listInsert(vm, l, index, vm->apiStack[2]);
		blPushNull(vm);
		return true;
	}

	NATIVE(bl_List_len) {
		blPushNumber(vm, AS_LIST(vm->apiStack[0])->count);
		return true;
	}

	NATIVE(bl_List_removeAt) {
		ObjList *l = AS_LIST(vm->apiStack[0]);
		size_t index = blCheckIndex(vm, 1, l->count, "i");
		if(index == SIZE_MAX) return false;

		Value r = l->arr[index];
		listRemove(vm, l, index);

		push(vm, r);
		return true;
	}

	NATIVE(bl_List_subList) {
		ObjList *list = AS_LIST(vm->apiStack[0]);

		size_t from = blCheckIndex(vm, 1, list->count, "from");
		if(from == SIZE_MAX) return false;
		size_t to = blCheckIndex(vm, 2, list->count + 1, "to");
		if(to == SIZE_MAX) return false;

		if(from >= to) BL_RAISE(vm, "InvalidArgException", "from must be < to.");

		size_t numElems = to - from;
		ObjList *subList = newList(vm, numElems < 16 ? 16 : numElems);

		memcpy(subList->arr, list->arr + from, numElems * sizeof(Value));
		subList->count = numElems;

		push(vm, OBJ_VAL(subList));
		return true;
	}

	NATIVE(bl_List_clear) {
		AS_LIST(vm->apiStack[0])->count = 0;
		blPushNull(vm);
		return true;
	}

	NATIVE(bl_List_iter) {
		ObjList *lst = AS_LIST(vm->apiStack[0]);

		if(blIsNull(vm, 1)) {
			if(lst->count == 0) {
				blPushBoolean(vm, false);
				return true;
			}
			blPushNumber(vm, 0);
			return true;
		}

		if(blIsNumber(vm, 1)) {
			double idx = blGetNumber(vm, 1);
			if(idx >= 0 && idx < lst->count - 1) {
				blPushNumber(vm, idx + 1);
				return true;
			}
		}

		blPushBoolean(vm, false);
		return true;
	}

	NATIVE(bl_List_next) {
		ObjList *lst = AS_LIST(vm->apiStack[0]);

		if(blIsNumber(vm, 1)) {
			double idx = blGetNumber(vm, 1);
			if(idx >= 0 && idx < lst->count) {
				push(vm, lst->arr[(size_t)idx]);
				return true;
			}
		}

		blPushNull(vm);
		return true;
	}
// } List

// class Tuple {
	NATIVE(bl_Tuple_len) {
		blPushNumber(vm, AS_TUPLE(vm->apiStack[0])->size);
		return true;
	}

	NATIVE(bl_Tuple_iter) {
		ObjTuple *tup = AS_TUPLE(vm->apiStack[0]);

		if(blIsNull(vm, 1)) {
			if(tup->size == 0) {
				blPushBoolean(vm, false);
				return true;
			}
			blPushNumber(vm, 0);
			return true;
		}

		if(blIsNumber(vm, 1)) {
			double idx = blGetNumber(vm, 1);
			if(idx >= 0 && idx < tup->size - 1) {
				blPushNumber(vm, idx + 1);
				return true;
			}
		}

		blPushBoolean(vm, false);
		return true;
	}

	NATIVE(bl_Tuple_next) {
		ObjTuple *tup = AS_TUPLE(vm->apiStack[0]);

		if(blIsNumber(vm, 1)) {
			double idx = blGetNumber(vm, 1);
			if(idx >= 0 && idx < tup->size) {
				push(vm, tup->arr[(size_t)idx]);
				return true;
			}
		}

		blPushNull(vm);
		return true;
	}
// }

// class String {
	NATIVE(bl_substr) {
		ObjString *str = AS_STRING(vm->apiStack[0]);

		size_t from = blCheckIndex(vm, 1, str->length + 1, "from");
		if(from == SIZE_MAX) return false;
		size_t to = blCheckIndex(vm, 2, str->length + 1, "to");
		if(to == SIZE_MAX) return false;

		if(from > to) BL_RAISE(vm, "InvalidArgException", "argument to must be >= from.");

		size_t len = to - from;
		ObjString *sub = allocateString(vm, len);
		memcpy(sub->data, str->data + from, len);

		push(vm, OBJ_VAL(sub));
		return true;
	}

	NATIVE(bl_String_join) {
		if(!blCheckList(vm, 1, "lst")) return false;

		ObjString *sep = AS_STRING(vm->apiStack[0]);
		ObjList *strings = AS_LIST(vm->apiStack[1]);

		size_t length = 0;
		for(size_t i = 0; i < strings->count; i++) {
			if(!IS_STRING(strings->arr[i]))
				BL_RAISE(vm, "TypeException", "All elements in iterable must be strings.");
			
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

		push(vm, OBJ_VAL(joined));
		return true;
	}

	NATIVE(bl_String_len) {
		blPushNumber(vm, blGetStringSz(vm, 0));
		return true;
	}

	NATIVE(bl_String_string) {
		return true;
	}

	NATIVE(bl_String_hash) {
		blPushNumber(vm, stringGetHash(AS_STRING(vm->apiStack[0])));
		return true;
	}

	NATIVE(bl_String_eq) {
		if(!blIsString(vm, 1)) {
			blPushBoolean(vm, false);
			return true;
		}
		
		ObjString *s1 = AS_STRING(vm->apiStack[0]);
		ObjString *s2 = AS_STRING(vm->apiStack[1]);

		if(s1->interned && s2->interned) {
			blPushBoolean(vm, s1 == s2);
			return true;
		}

		if(s1->length != s2->length) {
			blPushBoolean(vm, false);
			return true;
		}

		blPushBoolean(vm, memcmp(s1->data, s2->data, s1->length) == 0);
		return true;
	}

	NATIVE(bl_String_iter) {
		ObjString *s = AS_STRING(vm->apiStack[0]);

		if(blIsNull(vm, 1)) {
			if(s->length == 0) {
				blPushBoolean(vm, false);
				return true;
			}
			blPushNumber(vm, 0);
			return true;
		}

		if(blIsNumber(vm, 1)) {
			double idx = blGetNumber(vm, 1);
			if(idx >= 0 && idx < s->length - 1) {
				blPushNumber(vm, idx + 1);
				return true;
			}
		}

		blPushBoolean(vm, false);
		return true;
	}

	NATIVE(bl_String_next) {
		ObjString *str = AS_STRING(vm->apiStack[0]);

		if(blIsNumber(vm, 1)) {
			double idx = blGetNumber(vm, 1);
			if(idx >= 0 && idx < str->length) {
				blPushStringSz(vm, str->data + (size_t)idx, 1);
				return true;
			}
		}

		blPushNull(vm);
		return true;
	}
// } String

// class range {
	NATIVE(bl_range_new) {
		if(blIsNull(vm, 2)) {
			blPushNumber(vm, 0);
			push(vm, vm->apiStack[1]);
		} else {
			push(vm, vm->apiStack[1]);
			push(vm, vm->apiStack[2]);
		}

		push(vm, vm->apiStack[3]);
		
		if(!blCheckInt(vm, -3, "from")) return false;
		if(!blCheckInt(vm, -2, "to"))   return false;
		if(!blCheckInt(vm, -1, "step")) return false;

		push(vm, OBJ_VAL(newRange(vm, blGetNumber(vm, -3), blGetNumber(vm, -2), blGetNumber(vm, -1))));
		return true;
	}

	NATIVE(bl_range_iter) {
		ObjRange *r = AS_RANGE(vm->apiStack[0]);

		bool incr = r->step > 0;

		if(blIsNull(vm, 1)) {
			if(incr ? r->start < r->stop : r->start > r->stop) {
				blPushNumber(vm, r->start);
				return true;
			} else {
				blPushBoolean(vm, false);
				return true;
			}
		}

		if(blIsNumber(vm, 1)) {
			double i = blGetNumber(vm, 1) + r->step;
			
			if(incr ? i < r->stop : i > r->stop) {
				blPushNumber(vm, i);
				return true;
			}
		}

		blPushBoolean(vm, false);
		return true;
	}

	NATIVE(bl_range_next) {
		if(blIsNumber(vm, 1)) return true;
		blPushNull(vm);
		return true;
	}
// }
