#include "sys.h"
#include "vm.h"

#include "memory.h"

#include <string.h>
#include <stdio.h>

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

NATIVE(bl_gets) {
	char *str = GC_ALLOC(vm, 16);
	size_t strl = 16;
	size_t strsz = 0;

	int c;
	while((c = getc(stdin)) != EOF && c != '\n') {
		if(strsz + 1 > strl) {
			GCallocate(vm, str, strl, strl * 2);
			strl *= 2;
		}
		str[strsz++] = c;
	}

	if(strsz + 1 > strl) {
		GCallocate(vm, str, strl, strl + 1);
		strl += 1;
	}
	str[strsz] = '\0';

	BL_RETURN(OBJ_VAL(newStringFromBuf(vm, str, strsz)));
}

NATIVE(bl_init) {
	// Set up the standard I/O streams (this is a little bit of an hack)
	Value file;
	blGetGlobal(vm, "file", &file);

	Value fileCls;
	hashTableGet(&AS_MODULE(file)->globals, copyString(vm, "File", 4), &fileCls);

	ObjInstance *fileout = newInstance(vm, AS_CLASS(fileCls));
	blSetField(vm, fileout, "_handle", HANDLE_VAL(stdout));
	blSetField(vm, fileout, "_closed", BOOL_VAL(false));
	blSetGlobal(vm, "stdout", OBJ_VAL(fileout));

	ObjInstance *filein = newInstance(vm, AS_CLASS(fileCls));
	blSetField(vm, filein, "_handle", HANDLE_VAL(stdin));
	blSetField(vm, filein, "_closed", BOOL_VAL(false));
	blSetGlobal(vm, "stdin", OBJ_VAL(filein));

	ObjInstance *fileerr = newInstance(vm, AS_CLASS(fileCls));
	blSetField(vm, fileerr, "_handle", HANDLE_VAL(stderr));
	blSetField(vm, fileerr, "_closed", BOOL_VAL(false));
	blSetGlobal(vm, "stderr", OBJ_VAL(fileerr));

	// Set up command line arguments
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
