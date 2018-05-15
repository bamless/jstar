#include "modules.h"
#include "object.h"
#include "native.h"

#include "os.h"
#include "os.bl.h"

typedef enum {
	FUNC, CLASS
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

#define MODULE(name)       { #name, &name##_bl, {
#define ENDMODULE          { 0, .function = {NULL, NULL} } } },

#define CLASS(name)        { CLASS, .class = { #name, {
#define METHOD(name, fn)   {#name, fn},
#define ENDCLASS           { NULL, NULL } } } },

#define FUNCTION(name, fn) { FUNC, .function = { #name, fn } },

Module builtInModules[] = {
	MODULE(os)
		FUNCTION(getOS, &getOS)
	ENDMODULE
};
