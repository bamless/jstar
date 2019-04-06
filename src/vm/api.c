#include "blang.h"
#include "util.h"
#include "import.h"
#include "vm.h"

#include <string.h>

static void validateStack(BlangVM *vm) {
	assert(vm->sp - vm->stack != vm->stackSz, "Stack overflow");
}

void blPushNumber(BlangVM *vm, double number) {
	validateStack(vm);
	push(vm, NUM_VAL(number));
}

void blPushBoolean(BlangVM *vm, bool boolean) {
	validateStack(vm);
	push(vm, BOOL_VAL(boolean));
}

void blPushStringSz(BlangVM *vm, const char *string, size_t length) {
	validateStack(vm);
	push(vm, OBJ_VAL(copyString(vm, string, length, false)));
}
void blPushString(BlangVM *vm, const char *string) {
	blPushStringSz(vm, string, strlen(string));
}

void pushBoolean(BlangVM *vm, bool b) {
	validateStack(vm);
	push(vm, b ? TRUE_VAL : FALSE_VAL);
}

void blPushNull(BlangVM *vm) {
	validateStack(vm);
	push(vm, NULL_VAL);
}

void blPushValue(BlangVM *vm, int slot) {
	push(vm, apiStackSlot(vm, slot));
}

void blPop(BlangVM *vm) {
	assert(vm->sp > vm->apiStack, "Popping past frame boundary");
	pop(vm);
}

void blSetGlobal(BlangVM *vm, const char *module, const char *name) {
	ObjModule *mod = vm->module;
	if(module != NULL) mod = getModule(vm, copyString(vm, module, strlen(module), false));
	hashTablePut(&mod->globals, copyString(vm, name, strlen(name), false), peek(vm));
}

bool blGetGlobal(BlangVM *vm, const char *module, const char *name) {
	ObjModule *mod = vm->module;
	if(module != NULL) mod = getModule(vm, copyString(vm, module, strlen(module), false));
	
	ObjString *namestr = copyString(vm, name, strlen(name), false);

	Value res;
	if(!hashTableGet(&mod->globals, namestr, &res)) {
		if(!hashTableGet(&vm->core->globals, namestr, &res)) {
			blRaise(vm, "NameException", "Name %s not definied in module %s.", name, module);
			return false;
		}
	}

	push(vm, res);
	return true;
}

double blGetNumber(BlangVM *vm, int slot) {
	return AS_NUM(apiStackSlot(vm, slot));
}

const char *blGetString(BlangVM *vm, int slot) {
	return AS_STRING(apiStackSlot(vm, slot))->data;
}

size_t blGetStringSz(BlangVM *vm, int slot) {
	return AS_STRING(apiStackSlot(vm, slot))->length;
}

bool blGetBoolean(BlangVM *vm, int slot) {
	return AS_BOOL(apiStackSlot(vm, slot));
}

void *blGetHandle(BlangVM *vm, int slot) {
	return AS_HANDLE(apiStackSlot(vm, slot));
}

bool blIsNumber(BlangVM *vm, int slot) {
	return IS_NUM(apiStackSlot(vm, slot));
}

bool blIsInteger(BlangVM *vm, int slot) {
	return IS_INT(apiStackSlot(vm, slot));
}

bool blIsString(BlangVM *vm, int slot) {
	return IS_STRING(apiStackSlot(vm, slot));
}

bool blIsList(BlangVM *vm, int slot) {
	return IS_LIST(apiStackSlot(vm, slot));
}

bool blIsBoolean(BlangVM *vm ,int slot) {
	return IS_BOOL(apiStackSlot(vm,slot));
}

bool blIsNull(BlangVM *vm, int slot) {
	return IS_NULL(apiStackSlot(vm, slot));
}

bool blIsInstance(BlangVM *vm, int slot) {
	return IS_INSTANCE(apiStackSlot(vm, slot));
}

bool blIsHandle(BlangVM *vm, int slot) {
	return IS_HANDLE(apiStackSlot(vm, slot));
}

bool blCheckNum(BlangVM *vm, int slot, const char *name) {
	if(!blIsInteger(vm, slot)) BL_RAISE(vm, "TypeException", "%s must be a number.", name);
	return true;
}

bool blCheckInt(BlangVM *vm, int slot, const char *name) {
	if(!blIsInteger(vm, slot)) BL_RAISE(vm, "TypeException", "%s must be an integer.", name);
	return true;
}

bool blCheckStr(BlangVM *vm, int slot, const char *name) {
	if(!blIsString(vm, slot)) BL_RAISE(vm, "TypeException", "%s must be a String.", name);
	return true;
}

bool blCheckList(BlangVM *vm, int slot, const char *name) {
	if(!blIsList(vm, slot)) BL_RAISE(vm, "TypeException", "%s must be a List.", name);
	return true;
}

bool blCheckBool(BlangVM *vm, int slot, const char *name) {
	if(!blIsBoolean(vm, slot)) BL_RAISE(vm, "TypeException", "%s must be a String.", name);
	return true;
}

bool blCheckInstance(BlangVM *vm, int slot, const char *name) {
	if(!blIsInstance(vm, slot)) BL_RAISE(vm, "TypeException", "%s must be an Instance.", name);
	return false;
}

bool blCheckHandle(BlangVM *vm, int slot, const char *name) {
	if(!blIsHandle(vm, slot)) BL_RAISE(vm, "TypeException", "%s must be an Instance.", name);
	return false;
}

size_t blCheckIndex(BlangVM *vm, int slot, size_t max, const char *name) {
	if(!blCheckInt(vm, slot, name)) return SIZE_MAX;

	double i = blGetNumber(vm, slot);
	if(i >= 0 && i < max) return (size_t) i;

	blRaise(vm, "IndexOutOfBoundException", "%g.", i);
	return SIZE_MAX;
}