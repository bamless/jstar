#include "core.h"
#include "vm.h"

#include "modules.h"

void initCoreLibrary(VM *vm) {
	evaluateModule(vm, "__core__", readBuiltInModule("__core__"));
}

NATIVE(bl_error) {
	blRuntimeError(vm, AS_STRING(args[1])->data);
	return NULL_VAL;
}
