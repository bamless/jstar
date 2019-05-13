#ifndef BLANG_H
#define BLANG_H

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#define BLANG_VERSION_MAJOR 0
#define BLANG_VERSION_MINOR 3
#define BLANG_VERSION_PATCH 4

#define BLANG_VERSION_STRING "0.3.4"

#define BLANG_VERSION                                                                              \
    (BLANG_VERSION_MAJOR * 100000 + BLANG_VERSION_MINOR * 1000 + BLANG_VERSION_PATCH)

/**
 * =========================================================
 * Interpreter entry points
 * =========================================================
 */

typedef struct BlangVM BlangVM;

typedef enum {
    VM_EVAL_SUCCSESS, // The VM successfully executed the code
    VM_SYNTAX_ERR,    // A syntax error has been encountered in parsing
    VM_COMPILE_ERR,   // An error has been encountered during compilation
    VM_RUNTIME_ERR,   // An unhandled exception has reached the top of the stack
} EvalResult;

// Allocate a new VM with all the state needed for code execution
BlangVM *blNewVM();
// Free a previously obtained VM along with all the state
void blFreeVM(BlangVM *vm);

// Evaluate blang code in the context of module (or __main__ in blEvaluate)
// as a top level function (<main> function).
// VM_EVAL_SUCCSESS will be returned if the execution completed normally
// In case of errors, either VM_SYNTAX_ERR, VM_COMPILE_ERR or VM_RUNTIME_ERR
// will be returned, and all the errors will be printed to stderr.
EvalResult blEvaluate(BlangVM *vm, const char *fpath, const char *src);
EvalResult blEvaluateModule(BlangVM *vm, const char *fpath, const char *name, const char *src);

// Call a function (or method with name "name") that sits on the top of the stack
// along with its arguments. The state of the stack when calling should be:
//  ... [callee][arg1][arg2]...[argn] $top
//         |       |______________|
//         |                |
// function/instance  the args of the function/method
//
// In case of success VM_EVAL_SUCCSESS will be returned, and the result will be placed
// on the top of the stack in the place of "callee", popping all arguments:
//  ... [result] $top [arg1][arg2] ... [argn] popped
//
// If an exception has been raised by the code, VM_RUNTIME_ERR will be returned and
// The exception will be placed on top of the stack as a result.
EvalResult blCall(BlangVM *vm, uint8_t argc);
EvalResult blCallMethod(BlangVM *vm, const char *name, uint8_t argc);

// Init the sys.args list with a list of arguments (usually main arguments)
void blInitCommandLineArgs(int argc, const char **argv);
// Add a path to be searched during module imports
void blAddImportPath(BlangVM *vm, const char *path);

/**
 * =========================================================
 * Native function API
 * =========================================================
 */

// The minimum reserved space for the stack when calling a native function
#define MIN_NATIVE_STACK_SZ 20

// Utility macro for declaring/defining a native function
#define NATIVE(name) bool name(BlangVM *vm)

// Utility macro for raising an exception from a native function.
// It raises the exception and exits signaling the error.
#define BL_RAISE(vm, cls, err, ...)                                                                \
    do {                                                                                           \
        blRaise(vm, cls, err, ##__VA_ARGS__);                                                      \
        return false;                                                                              \
    } while(0)

// Main module and core module names
#define MAIN_MODULE "__main__"
#define CORE_MODULE "__core__"

// Ensure `needed` slots are available on the stack
void ensureStack(BlangVM *vm, size_t needed);

// A C function callable from blang
typedef bool (*Native)(BlangVM *vm);

// Instantiate an exception from "cls" with "err" as an error string and raises
// it, leaving it on top of the stack.
// If "cls" cannot be found in current module a NameException is raised instead.
void blRaise(BlangVM *vm, const char *cls, const char *err, ...);

// Check if two blang values are equal.
// As this function may call the __eq__ method, it behaves like
// blCall, i.e. the two values should be on the top of the stack
// when calling, and the result will be left on the top of the
// stack popping the two values.
// This function will return true if the execution was successful,
// And false if an exception was raised, leaving the result or
// the exception on top of the stack repectively.
bool blEquals(BlangVM *vm);

// Check if a value is of a certain class.
bool blIs(BlangVM *vm, int slot, int classSlot);

// Functions used for iterating over a generic iterable.
// `iterable` is the slot in which the iterable object is sitting and `res` is slot of the 
// result of the last blIter call or, if first time calling blIter, a slot containing null.
// blNext is called to obtain the next element in the iteration. The element will be placed
// on the stack.
bool blIter(BlangVM *vm, int iterable, int res, bool *err);
bool blNext(BlangVM *vm, int iterable, int res);

// Macro that automatically configures the loop to iterate over a blang iterable using blIter and
// blNext.
// `iter` is the slot of the iterable we want to iterate over and `code` a block used as the body.
// Beware that the macro pushes a new value on top of the stack to store the result of blIter, so
// negative slot indeces to access previously pushed elements should be offset by one
#define blForEach(iter, code, cleanup) {                                                                    \
    bool _err = false;                                                                             \
    blPushNull(vm);                                                                                \
    while(blIter(vm, iter, -1, &_err)) {                                                           \
        if(_err || !blNext(vm, iter, -1)) {                                                        \
            cleanup;                                                                               \
            return false;                                                                          \
        }                                                                                          \
        code                                                                                       \
    }                                                                                              \
    blPop(vm);                                                                                     \
}                                                                                                  \

// Function for converting C values to Blang values.
// They leave the converted value on top of the stack

void blPushNumber(BlangVM *vm, double number);
void blPushBoolean(BlangVM *vm, bool boolean);
void blPushStringSz(BlangVM *vm, const char *string, size_t size);
void blPushString(BlangVM *vm, const char *string);
void pushBoolean(BlangVM *vm, bool b);
void blPushHandle(BlangVM *vm, void *handle);
void blPushNull(BlangVM *vm);
void blPushList(BlangVM *vm);
void blPushValue(BlangVM *vm, int slot);
#define blDup() blPushValue(vm, -1)

// Functions from converting from Blang values to C values

double blGetNumber(BlangVM *vm, int slot);
bool blGetBoolean(BlangVM *vm, int slot);
void *blGetHandle(BlangVM *vm, int slot);
size_t blGetStringSz(BlangVM *vm, int slot);
// WARNING: The returned string is owned by Blang
// and thus is garbage collected. Never use this
// buffer outside the native where it was retrieved.
// Also be careful when popping the string from the stack
// while retaining this buffer, because if a GC occurs
// and the string is not found to be reachable it'll be
// collected.
const char *blGetString(BlangVM *vm, int slot);

// Function for working with lists
// These functions do not perfrom bounds checking,
// use blCeckIndex first if needed.

void blListAppend(BlangVM *vm, int slot);
void blListInsert(BlangVM *vm, size_t i, int slot);
void blListRemove(BlangVM *vm, size_t i, int slot);
void blListGetLength(BlangVM *vm, int slot);
void blListGet(BlangVM *vm, size_t i, int slot);

// Set the field "name" of the value at "slot" with the value
// on top of the stack. the value is not popped.
void blSetField(BlangVM *vm, int slot, const char *name);
// Get the field "name" of the value at "slot".
// Returns true in case of success leaving the result on
// top of the stack, false otherwise leaving an exception
// on top of the stack.
bool blGetField(BlangVM *vm, int slot, const char *name);

// Set the global "name" of the module "mname" with the value
// on top of the stack. the value is not popped.
// If calling from inside a native mname can be NULL, and the
// used module will be the current one.
void blSetGlobal(BlangVM *vm, const char *mname, const char *name);
// Get the global "name" of the module "mname".
// Returns true in case of success leaving the result on the
// top of the stack, false otherwise leaving an exception on
// top of the stack.
// If calling from inside a native mname can be NULL, and the
// used module will be the current one.
bool blGetGlobal(BlangVM *vm, const char *mname, const char *name);

// Function for checking the type of a slot.
// These functions return true if the value at slot
// is of the given type, otherwise they return false
// and leave an exception on top of the stack

bool blIsNumber(BlangVM *vm, int slot);
bool blIsInteger(BlangVM *vm, int slot);
bool blIsString(BlangVM *vm, int slot);
bool blIsList(BlangVM *vm, int slot);
bool blIsTuple(BlangVM *vm, int slot);
bool blIsBoolean(BlangVM *vm, int slot);
bool blIsHandle(BlangVM *vm, int slot);
bool blIsNull(BlangVM *vm, int slot);
bool blIsInstance(BlangVM *vm, int slot);

bool blCheckNum(BlangVM *vm, int slot, const char *name);
bool blCheckInt(BlangVM *vm, int slot, const char *name);
bool blCheckStr(BlangVM *vm, int slot, const char *name);
bool blCheckList(BlangVM *vm, int slot, const char *name);
bool blCheckTuple(BlangVM *vm, int slot, const char *name);
bool blCheckBool(BlangVM *vm, int slot, const char *name);
bool blCheckInstance(BlangVM *vm, int slot, const char *name);
bool blCheckHandle(BlangVM *vm, int slot, const char *name);

// Check if the value at slot "slot" is an integer >= 0 and < max.
// Returns the number casted to size_t if true, SIZE_MAX if false
// leaving an exception on top of the stack.
size_t blCheckIndex(BlangVM *vm, int slot, size_t max, const char *name);

// Pop a value from the top of the stack
void blPop(BlangVM *vm);

// Prints the the stack trace of the exception on the top of the stack
void blPrintStackTrace(BlangVM *vm);

/**
 * =========================================================
 * Buffer creation and manipulation functions
 * =========================================================
 */

typedef struct BlBuffer {
    BlangVM *vm;
    size_t size;
    size_t len;
    char *data;
} BlBuffer;

void blBufferInit(BlangVM *vm, BlBuffer *b);
void blBufferInitSz(BlangVM *vm, BlBuffer *b, size_t size);
void blBufferAppend(BlBuffer *b, const char *str, size_t len);
void blBufferAppendstr(BlBuffer *b, const char *str);
void blBufferTrunc(BlBuffer *b, size_t len);
void blBufferCut(BlBuffer *b, size_t len);
void blBufferReplaceChar(BlBuffer *b, size_t start, char c, char r);
void blBufferPrepend(BlBuffer *b, const char *str, size_t len);
void blBufferPrependstr(BlBuffer *b, const char *str);
void blBufferAppendChar(BlBuffer *b, char c);
void blBufferClear(BlBuffer *b);

void blBufferPush(BlBuffer *b);
void blBufferFree(BlBuffer *b);

#endif