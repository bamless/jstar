#include "sys.h"

#include <string.h>

static int argCount = 0 ;
static const char **argVector = NULL;

void sysInitArgs(int argc, const char **argv) {
	argCount = argc;
	argVector = argv;
}

NATIVE(bl_platform) {
#ifdef __linux
	return OBJ_VAL(copyString(vm, "Linux", 5));
#elif _WIN32
	return OBJ_VAL(copyString(vm, "Win32", 5));
#elif __APPLE__
	return OBJ_VAL(copyString(vm, "OSX", 3));
#endif
}

NATIVE(bl_initArgs) {
	if(argCount == 0) return NULL_VAL;

	Value a;
	if(!blGetGlobal(vm, "args", &a)) {
		blRuntimeError(vm, "This shouldn't happend: sys.args not found.");
		return NULL_VAL;
	}

	ObjList *lst = AS_LIST(a);

	for(int i = 0; i < argCount; i++) {
		listAppend(vm, lst, OBJ_VAL(copyString(vm, argVector[i], strlen(argVector[i]))));
	}

	return NULL_VAL;
}
