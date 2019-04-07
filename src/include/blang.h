#ifndef BLANG_H
#define BLANG_H

#include <stdbool.h>
#include <stdlib.h>
#include <stdint.h>

typedef enum {
	VM_EVAL_SUCCSESS, // The VM successfully executed the code
	VM_SYNTAX_ERR,    // A syntax error has been encountered in parsing
	VM_COMPILE_ERR,   // An error has been encountered during compilation
	VM_RUNTIME_ERR,   // An unhandled exception has reached the top of the stack
} EvalResult;

typedef struct BlangVM BlangVM;

BlangVM *blNewVM();
void blFreeVM(BlangVM *vm);

EvalResult blEvaluate(BlangVM *vm, const char *fpath, const char *src);
EvalResult blEvaluateModule(BlangVM *vm, const char *fpath, const char *name, const char *src);

EvalResult blCall(BlangVM *vm, uint8_t argc);
EvalResult blCallMethod(BlangVM *vm, const char *name, uint8_t argc);

void blInitCommandLineArgs(int argc, const char **argv);
void blAddImportPath(BlangVM *vm, const char *path);

// Native API
#define MIN_NATIVE_STACK_SZ 20

#define NATIVE(name) bool name(BlangVM *vm)

#define BL_RAISE(vm, cls, err, ...) do { \
	blRaise(vm, cls, err, ##__VA_ARGS__); \
	return false; \
} while(0)

#define MAIN_MODULE "__main__"
#define CORE_MODULE "__core__"

typedef bool (*Native)(BlangVM *vm);

void blRaise(BlangVM *vm, const char* cls, const char *err, ...);

void blPushNumber(BlangVM *vm, double number);
void blPushBoolean(BlangVM *vm, bool boolean);
void blPushStringSz(BlangVM *vm, const char *string, size_t size);
void blPushString(BlangVM *vm, const char *string);
void pushBoolean(BlangVM *vm, bool b);
void blPushHandle(BlangVM *vm, void *handle);
void blPushNull(BlangVM *vm);
void blPushValue(BlangVM *vm, int slot);
#define blDup() blPushValue(vm, -1)

void blSetField(BlangVM *vm, int slot, const char *name);
bool blGetField(BlangVM *vm, int slot, const char *name);

void blSetGlobal(BlangVM *vm, const char *module, const char *name);
bool blGetGlobal(BlangVM *vm, const char *module, const char *name);

double blGetNumber(BlangVM *vm, int slot);
const char *blGetString(BlangVM *vm, int slot);
size_t blGetStringSz(BlangVM *vm, int slot);
bool blGetBoolean(BlangVM *vm, int slot);
void *blGetHandle(BlangVM *vm, int slot);

bool blIsNumber(BlangVM *vm, int slot);
bool blIsInteger(BlangVM *vm, int slot);
bool blIsString(BlangVM *vm, int slot);
bool blIsList(BlangVM *vm, int slot);
bool blIsBoolean(BlangVM *vm ,int slot);
bool blIsHandle(BlangVM *vm, int slot);
bool blIsNull(BlangVM *vm, int slot);
bool blIsInstance(BlangVM *vm, int slot);

bool blCheckNum(BlangVM *vm, int slot, const char *name);
bool blCheckInt(BlangVM *vm, int slot, const char *name);
bool blCheckStr(BlangVM *vm, int slot, const char *name);
bool blCheckList(BlangVM *vm, int slot, const char *name);
bool blCheckBool(BlangVM *vm, int slot, const char *name);
bool blCheckInstance(BlangVM *vm, int slot, const char *name);
bool blCheckHandle(BlangVM *vm, int slot, const char *name);

size_t blCheckIndex(BlangVM *vm, int slot, size_t max, const char *name);

void blPop(BlangVM *vm);

#endif