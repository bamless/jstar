#include "sys.h"
#include "memory.h"
#include "io.h"
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
	BlBuffer b;
	blBufferInit(vm, &b);

	int c;
	while((c = getc(stdin)) != EOF && c != '\n') {
		blBufferAppendChar(&b, c);
	}

	blBufferPush(&b);
	return true;
}

NATIVE(bl_init) {
	// Set up the standard I/O streams (this is a little bit of an hack)
	if(!blGetGlobal(vm, "io", "File")) return false;

	ObjInstance *fileout = newInstance(vm, AS_CLASS(vm->sp[-1]));
	push(vm, OBJ_VAL(fileout));

	blPushHandle(vm, (void*)stdout);
	blSetField(vm, -2, FIELD_FILE_HANDLE);
	blPop(vm);

	blPushBoolean(vm, false);
	blSetField(vm, -2, FIELD_FILE_CLOSED);
	blPop(vm);

	blSetGlobal(vm, NULL, "out");
	blPop(vm);

	ObjInstance *filein = newInstance(vm, AS_CLASS(vm->sp[-1]));
	push(vm, OBJ_VAL(filein));

	blPushHandle(vm, (void*)stdin);
	blSetField(vm, -2, FIELD_FILE_HANDLE);
	blPop(vm);

	blPushBoolean(vm, false);
	blSetField(vm, -2, FIELD_FILE_CLOSED);
	blPop(vm);

	blSetGlobal(vm, NULL, "stdin");
	blPop(vm);

	ObjInstance *fileerr = newInstance(vm, AS_CLASS(vm->sp[-1]));
	push(vm, OBJ_VAL(fileerr));

	blPushHandle(vm, (void*)stderr);
	blSetField(vm, -2, FIELD_FILE_HANDLE);
	blPop(vm);

	blPushBoolean(vm, false);
	blSetField(vm, -2, FIELD_FILE_CLOSED);
	blPop(vm);

	blSetGlobal(vm, NULL, "err");
	blPop(vm);

	// Set up command line arguments
	if(argCount != 0) {
		blGetGlobal(vm, NULL, "args");
		for(int i = 0; i < argCount; i++) {
			blPushString(vm, argVector[i]);
			blListAppend(vm, -2);
			blPop(vm);
		}
	}

	blPushNull(vm);
	return true;
}
