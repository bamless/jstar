#include "sys.h"
#include "vm.h"

#include "memory.h"

#include <string.h>

static int argCount = 0 ;
static const char **argVector = NULL;

void sysInitArgs(int argc, const char **argv) {
	argCount = argc;
	argVector = argv;
}

NATIVE(bl_exit) {
	if(!IS_INT(args[1])) {
		BL_RAISE_EXCEPTION(vm, "InvalidArgException", "Argrument must be an integer");
	}
	exit((int)AS_NUM(args[1]));
}

NATIVE(bl_getImportPaths) {
	BL_RETURN(OBJ_VAL(vm->importpaths));
}

NATIVE(bl_platform) {
#ifdef __linux
	BL_RETURN(OBJ_VAL(copyString(vm, "Linux", 5)));
#elif _WIN32
	BL_RETURN(OBJ_VAL(copyString(vm, "Win32", 5)));
#elif __APPLE__
	BL_RETURN(OBJ_VAL(copyString(vm, "OSX", 3)));
#endif
}

NATIVE(bl_gc) {
	garbageCollect(vm);
	BL_RETURN(NULL_VAL);
}

NATIVE(bl_initArgs) {
	if(argCount == 0) BL_RETURN(NULL_VAL);

	Value a;
	if(!blGetGlobal(vm, "args", &a)) {
		BL_RAISE_EXCEPTION(vm, "Exception", "This shouldn't happend: sys.args not found.");
	}

	ObjList *lst = AS_LIST(a);

	for(int i = 0; i < argCount; i++) {
		listAppend(vm, lst, OBJ_VAL(copyString(vm, argVector[i], strlen(argVector[i]))));
	}

	BL_RETURN(NULL_VAL);
}
