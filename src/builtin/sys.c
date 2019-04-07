#include "sys.h"
#include "memory.h"
#include "vm.h"

#include <string.h>
#include <stdio.h>
#include <time.h>

static int argCount = 0 ;
static const char **argVector = NULL;

void sysInitArgs(int argc, const char **argv) {
	argCount = argc;
	argVector = argv;
}

NATIVE(bl_exit) {
	if(!blCheckInt(vm, 1, "n")) return false;
	exit(blGetNumber(vm, 1));
}

NATIVE(bl_getImportPaths) {
	push(vm, OBJ_VAL(vm->importpaths));
	return true;
}

NATIVE(bl_platform) {
#ifdef __linux
	blPushString(vm, "Linux");
#elif _WIN32
	blPushString(vm, "Win32");
#elif __APPLE__
	blPushString(vm, "OSX");
#endif
	return true;
}

NATIVE(bl_clock) {
	blPushNumber(vm, (double) clock() / CLOCKS_PER_SEC);
	return true;
}

NATIVE(bl_gc) {
	garbageCollect(vm);
	blPushNull(vm);
	return true;
}

NATIVE(bl_gets) {
	ObjString *str = allocateString(vm, 16);
	size_t i = 0;

	int c;
	while((c = getc(stdin)) != EOF && c != '\n') {
		if(i + 1 > str->length) {
			reallocateString(vm, str, str->length * 2);
		}
		str->data[i++] = c;
	}

	if(str->length != i)
		reallocateString(vm, str, i);

	push(vm, OBJ_VAL(str));
	return true;
}

NATIVE(bl_init) {
	// Set up the standard I/O streams (this is a little bit of an hack)
	if(!blGetGlobal(vm, NULL, "file")) return false;

	if(!blGetField(vm, -1, "File")) return false;

	ObjInstance *fileout = newInstance(vm, AS_CLASS(vm->sp[-1]));
	push(vm, OBJ_VAL(fileout));

	blPushHandle(vm, (void*)stdout);
	blSetField(vm, -2, "_handle");
	blPop(vm);

	blPushBoolean(vm, false);
	blSetField(vm, -2, "_closed");
	blPop(vm);

	blSetGlobal(vm, NULL, "stdout");
	blPop(vm);

	ObjInstance *filein = newInstance(vm, AS_CLASS(vm->sp[-1]));
	push(vm, OBJ_VAL(filein));

	blPushHandle(vm, (void*)stdin);
	blSetField(vm, -2, "_handle");
	blPop(vm);

	blPushBoolean(vm, false);
	blSetField(vm, -2, "_closed");
	blPop(vm);

	blSetGlobal(vm, NULL, "stdin");
	blPop(vm);

	ObjInstance *fileerr = newInstance(vm, AS_CLASS(vm->sp[-1]));
	push(vm, OBJ_VAL(fileerr));

	blPushHandle(vm, (void*)stderr);
	blSetField(vm, -2, "_handle");
	blPop(vm);

	blPushBoolean(vm, false);
	blSetField(vm, -2, "_closed");
	blPop(vm);

	blSetGlobal(vm, NULL, "stderr");
	blPop(vm);

	// Set up command line arguments
	if(argCount != 0) {
		blGetGlobal(vm, NULL, "args");
		ObjList *lst = AS_LIST(vm->sp[-1]);

		for(int i = 0; i < argCount; i++) {
			listAppend(vm, lst, OBJ_VAL(copyString(vm, argVector[i], strlen(argVector[i]), false)));
		}
	}

	blPushNull(vm);
	return true;
}
