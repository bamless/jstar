#include "modules.h"
#include "object.h"

#include "core.h"
#include "core.bl.h"

#include "sys.h"
#include "sys.bl.h"

#include "file.h"
#include "file.bl.h"

#include "map.bl.h"
#include "set.bl.h"

#include <string.h>

typedef enum {
	TYPE_FUNC, TYPE_CLASS
} Type;

typedef struct {
	const char *name;
	Native func;
} Func;

typedef struct {
	const char *name;
	Func methods[16];
} Class;

typedef struct {
	Type type;
	union {
		Func  function;
		Class class;
	};
} ModuleElem;

typedef struct {
	const char *name;
	const char **src;
	ModuleElem elems[21];
} Module;

#define ELEMS_END        {TYPE_FUNC, .function = METHODS_END},
#define MODULES_END      {NULL, NULL, { ELEMS_END }}
#define METHODS_END      {NULL, NULL}

#define MODULE(name)     { #name, &name##_bl, {
#define ENDMODULE        ELEMS_END } },

#define COREMODULE       {"__core__", &core_bl, {

#define CLASS(name)      { TYPE_CLASS, .class = { #name, {
#define METHOD(name, fn) { #name, fn },
#define ENDCLASS         METHODS_END } } },

#define FUNCTION(name, fn) { TYPE_FUNC, .function = { #name, fn } },

Module builtInModules[] = {
	COREMODULE
		FUNCTION(typeassert, &bl_typeassert)
		FUNCTION(typeassertInt, &bl_typeassertInt)
		FUNCTION(int, &bl_int)
		FUNCTION(num, &bl_num)
		FUNCTION(list, &bl_list)
		FUNCTION(error, &bl_error)
		FUNCTION(isInt, &bl_isInt)
		FUNCTION(__printstr, &bl_printstr)
		CLASS(Number)
			METHOD(__string__, &bl_Number_string)
			METHOD(__class__, &bl_Number_class)
			METHOD(__hash__, &bl_Number_hash)
		ENDCLASS
		CLASS(Boolean)
			METHOD(__string__, &bl_Boolean_string)
			METHOD(__class__, &bl_Boolean_class)
		ENDCLASS
		CLASS(Null)
			METHOD(__string__, &bl_Null_string)
			METHOD(__class__, &bl_Null_class)
		ENDCLASS
		CLASS(Function)
			METHOD(__string__, &bl_Function_string)
		ENDCLASS
		CLASS(Module)
			METHOD(__string__, &bl_Module_string)
		ENDCLASS
		CLASS(List)
			METHOD(add, &bl_List_add)
			METHOD(insert, &bl_List_insert)
			METHOD(size, &bl_List_size)
			METHOD(removeAt, &bl_List_removeAt)
			METHOD(clear,  &bl_List_clear)
		ENDCLASS
		CLASS(String)
			METHOD(substr, &bl_substr)
			METHOD(length, &bl_String_length)
			METHOD(__hash__, &bl_String_hash)
		ENDCLASS
	ENDMODULE
	MODULE(sys)
		FUNCTION(platform, &bl_platform)
		FUNCTION(__initArgs, &bl_initArgs)
	ENDMODULE
	MODULE(file)
		CLASS(File)
			METHOD(size, &bl_File_size)
			METHOD(readAll, &bl_File_readAll)
			METHOD(readLine, &bl_File_readLine)
			METHOD(close, &bl_File_close)
		ENDCLASS
		FUNCTION(__open, &bl_open)
	ENDMODULE
	MODULE(map) ENDMODULE
	MODULE(set) ENDMODULE
	MODULES_END
};

static Module* getModule(const char *name) {
	for(int i = 0; builtInModules[i].name != NULL; i++) {
		if(strcmp(name, builtInModules[i].name) == 0) {
			return &builtInModules[i];
		}
	}
	return NULL;
}

static Class *getClass(Module *module, const char *name) {
	for(int i = 0;; i++) {
		ModuleElem *e = &module->elems[i];
		if(e->type == TYPE_FUNC && e->function.name == NULL) return NULL;

		if(e->type == TYPE_CLASS) {
			if(strcmp(module->elems[i].class.name, name) == 0) {
				return &module->elems[i].class;
			}
		}
	}
}

static Native getNativeMethod(Class *cls, const char *name) {
	for(int i = 0; cls->methods[i].name != NULL; i++) {
		if(strcmp(cls->methods[i].name, name) == 0) {
			return cls->methods[i].func;
		}
	}
	return NULL;
}

static Native getNativeFunc(Module *module, const char *name) {
	for(int i = 0;; i++) {
		if(module->elems[i].type == TYPE_FUNC) {
			if(module->elems[i].function.name == NULL) return NULL;

			if(strcmp(module->elems[i].function.name, name) == 0) {
				return module->elems[i].function.func;
			}
		}
	}
}

Native resolveBuiltIn(const char *module, const char *cls, const char *name) {
	Module *m = getModule(module);
	if(m == NULL) return NULL;

	if(cls == NULL) {
		return getNativeFunc(m, name);
	}

	Class *c = getClass(m, cls);
	if(c == NULL) return NULL;

	return getNativeMethod(c, name);
}

const char *readBuiltInModule(const char *name) {
	Module *m = getModule(name);
	if(m != NULL) {
		return *m->src;
	}
	return NULL;
}
