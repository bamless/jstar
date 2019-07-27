// MIT License

// Copyright (c) 2018 Fabrizio Pietrucci

// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:

// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.

// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#ifndef BLANG_H
#define BLANG_H

#include "blconf.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

/**
 * =========================================================
 *  Blang VM entry points
 * =========================================================
 */

// The Blang virtual machine
typedef struct BlangVM BlangVM;

typedef enum {
    VM_EVAL_SUCCSESS, // The VM successfully executed the code
    VM_SYNTAX_ERR,    // A syntax error has been encountered in parsing
    VM_COMPILE_ERR,   // An error has been encountered during compilation
    VM_RUNTIME_ERR,   // An unhandled exception has reached the top of the stack
} EvalResult;

// Allocate a new VM with all the state needed for code execution
BLANG_API BlangVM *blNewVM();
// Free a previously obtained VM along with all the state
BLANG_API void blFreeVM(BlangVM *vm);

// Evaluate blang code in the context of module (or __main__ in blEvaluate)
// as top level <main> function.
// VM_EVAL_SUCCSESS will be returned if the execution completed normally
// In case of errors, either VM_SYNTAX_ERR, VM_COMPILE_ERR or VM_RUNTIME_ERR
// will be returned, and all the errors will be printed to stderr.
BLANG_API EvalResult blEvaluate(BlangVM *vm, const char *fpath, const char *src);
BLANG_API EvalResult blEvaluateModule(BlangVM *vm, const char *fpath, const char *name, const char *src);

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
BLANG_API EvalResult blCall(BlangVM *vm, uint8_t argc);
BLANG_API EvalResult blCallMethod(BlangVM *vm, const char *name, uint8_t argc);

// Prints the the stack trace of the exception on the top of the stack
BLANG_API void blPrintStackTrace(BlangVM *vm);
// Init the sys.args list with a list of arguments (usually main arguments)
BLANG_API void blInitCommandLineArgs(int argc, const char **argv);
// Add a path to be searched during module imports
BLANG_API void blAddImportPath(BlangVM *vm, const char *path);

/**
 * =========================================================
 *  Native function API
 * =========================================================
 */

// ---- Utility functions and definitions ----

// The minimum reserved space for the stack when calling a native function
#define MIN_NATIVE_STACK_SZ 20

// Utility macro for declaring/defining a native function
#define NATIVE(name) bool name(BlangVM *vm)

// Utility macro for raising an exception from a native function.
// It raises the exception and exits signaling the error.
#define BL_RAISE(vm, cls, err, ...)           \
    do {                                      \
        blRaise(vm, cls, err, ##__VA_ARGS__); \
        return false;                         \
    } while(0)

// Main module and core module names
#define MAIN_MODULE "__main__"
#define CORE_MODULE "__core__"

// Ensure `needed` slots are available on the stack
BLANG_API void ensureStack(BlangVM *vm, size_t needed);

// A C function callable from blang
typedef bool (*Native)(BlangVM *vm);

// ---- Overloadable operator functions ----

// Instantiate an exception from "cls" with "err" as an error string and raises
// it, leaving it on top of the stack.
// If "cls" cannot be found in current module a NameException is raised instead.
BLANG_API void blRaise(BlangVM *vm, const char *cls, const char *err, ...);

// Check if two blang values are equal.
// As this function may call the __eq__ method, it behaves like
// blCall, i.e. the two values should be on the top of the stack
// when calling, and the result will be left on the top of the
// stack popping the two values.
// This function will return true if the execution was successful,
// And false if an exception was raised, leaving the result or
// the exception on top of the stack repectively.
BLANG_API bool blEquals(BlangVM *vm);

// Check if a value is of a certain class.
BLANG_API bool blIs(BlangVM *vm, int slot, int classSlot);

// ---- Iterable protocol functions ----

// `iterable` is the slot in which the iterable object is sitting and `res` is the slot of the
// result of the last blIter call or, if first time calling blIter, a slot containing null.
// blNext is called to obtain the next element in the iteration. The element will be placed
// on the top of the stack.
BLANG_API bool blIter(BlangVM *vm, int iterable, int res, bool *err);
BLANG_API bool blNext(BlangVM *vm, int iterable, int res);

// Macro that automatically configures the loop to iterate over a blang iterable using blIter and
// blNext.
// `iter` is the slot of the iterable we want to iterate over and `code` a block used as the body.
// Beware that the macro pushes a new value on top of the stack to store the result of blIter, so
// negative slot indeces to access previously pushed elements should be offset by one
#define blForEach(iter, code, cleanup)          \
    {                                           \
        bool _err = false;                      \
        blPushNull(vm);                         \
        while(blIter(vm, iter, -1, &_err)) {    \
            if(_err || !blNext(vm, iter, -1)) { \
                cleanup;                        \
                return false;                   \
            }                                   \
            code                                \
        }                                       \
        blPop(vm);                              \
    }

// ---- C to Blang values converting functions ----

// The converted value is left on the top of the stack

BLANG_API void blPushNumber(BlangVM *vm, double number);
BLANG_API void blPushBoolean(BlangVM *vm, bool boolean);
BLANG_API void blPushStringSz(BlangVM *vm, const char *string, size_t size);
BLANG_API void blPushString(BlangVM *vm, const char *string);
BLANG_API void pushBoolean(BlangVM *vm, bool b);
BLANG_API void blPushHandle(BlangVM *vm, void *handle);
BLANG_API void blPushNull(BlangVM *vm);
BLANG_API void blPushList(BlangVM *vm);
BLANG_API void blPushValue(BlangVM *vm, int slot);
#define blDup() blPushValue(vm, -1)

// ---- Blang to C values converter functions ----

BLANG_API double blGetNumber(BlangVM *vm, int slot);
BLANG_API bool blGetBoolean(BlangVM *vm, int slot);
BLANG_API void *blGetHandle(BlangVM *vm, int slot);
BLANG_API size_t blGetStringSz(BlangVM *vm, int slot);

// BEWARE: The returned string is owned by Blang
// and thus is garbage collected. Never use this
// buffer outside the native where it was retrieved.
// Also be careful when popping the original ObjString
// from the stack  while retaining this buffer, because 
// if a GC occurs and the string is not found to be 
// reachable it'll be collected.
BLANG_API const char *blGetString(BlangVM *vm, int slot);

// ---- List manipulation functions ----

// These functions do not perfrom bounds checking,
// use blCeckIndex first if needed.

BLANG_API void blListAppend(BlangVM *vm, int slot);
BLANG_API void blListInsert(BlangVM *vm, size_t i, int slot);
BLANG_API void blListRemove(BlangVM *vm, size_t i, int slot);
BLANG_API void blListGetLength(BlangVM *vm, int slot);
BLANG_API void blListGet(BlangVM *vm, size_t i, int slot);

// ---- Object instances manipulation functions ----

// Set the field "name" of the value at "slot" with the value
// on top of the stack. the value is not popped.
BLANG_API void blSetField(BlangVM *vm, int slot, const char *name);

// Get the field "name" of the value at "slot".
// Returns true in case of success leaving the result on
// top of the stack, false otherwise leaving an exception
// on top of the stack.
BLANG_API bool blGetField(BlangVM *vm, int slot, const char *name);

// ---- Modules manipulation functions ----

// Set the global "name" of the module "mname" with the value
// on top of the stack. the value is not popped.
// If calling from inside a native mname can be NULL, and the
// used module will be the current one.
BLANG_API void blSetGlobal(BlangVM *vm, const char *mname, const char *name);

// Get the global "name" of the module "mname".
// Returns true in case of success leaving the result on the
// top of the stack, false otherwise leaving an exception on
// top of the stack.
// If calling from inside a native mname can be NULL, and the
// used module will be the current one.
BLANG_API bool blGetGlobal(BlangVM *vm, const char *mname, const char *name);

// ---- Blang type checking functions ----

// These functions return true if the slot is of the given type, false otherwise
BLANG_API bool blIsNumber(BlangVM *vm, int slot);
BLANG_API bool blIsInteger(BlangVM *vm, int slot);
BLANG_API bool blIsString(BlangVM *vm, int slot);
BLANG_API bool blIsList(BlangVM *vm, int slot);
BLANG_API bool blIsTuple(BlangVM *vm, int slot);
BLANG_API bool blIsBoolean(BlangVM *vm, int slot);
BLANG_API bool blIsHandle(BlangVM *vm, int slot);
BLANG_API bool blIsNull(BlangVM *vm, int slot);
BLANG_API bool blIsInstance(BlangVM *vm, int slot);

// These functions return true if the slot is of the given type, false otherwise leaving a
// TypeException on top of the stack with a message customized with 'name'
BLANG_API bool blCheckNum(BlangVM *vm, int slot, const char *name);
BLANG_API bool blCheckInt(BlangVM *vm, int slot, const char *name);
BLANG_API bool blCheckStr(BlangVM *vm, int slot, const char *name);
BLANG_API bool blCheckList(BlangVM *vm, int slot, const char *name);
BLANG_API bool blCheckTuple(BlangVM *vm, int slot, const char *name);
BLANG_API bool blCheckBool(BlangVM *vm, int slot, const char *name);
BLANG_API bool blCheckInstance(BlangVM *vm, int slot, const char *name);
BLANG_API bool blCheckHandle(BlangVM *vm, int slot, const char *name);

// Check if the value at slot "slot" is an integer >= 0 and < max.
// Returns the number casted to size_t if true, SIZE_MAX if false
// leaving an exception on top of the stack.
BLANG_API size_t blCheckIndex(BlangVM *vm, int slot, size_t max, const char *name);

// Pop a value from the top of the stack
BLANG_API void blPop(BlangVM *vm);

/**
 * =========================================================
 *  Buffer creation and manipulation functions
 * =========================================================
 */

// Dynamic Buffer that holds memory allocated by the Blang garbage collector.
// This memory is owned by Blang, but cannot be collected until the buffer 
// is pushed on the stack using the blBufferPush method.
// Used for efficient creation of Strings in the native API.
typedef struct BlBuffer {
    BlangVM *vm;
    size_t size;
    size_t len;
    char *data;
} BlBuffer;

BLANG_API void blBufferInit(BlangVM *vm, BlBuffer *b);
BLANG_API void blBufferInitSz(BlangVM *vm, BlBuffer *b, size_t size);
BLANG_API void blBufferAppend(BlBuffer *b, const char *str, size_t len);
BLANG_API void blBufferAppendstr(BlBuffer *b, const char *str);
BLANG_API void blBufferTrunc(BlBuffer *b, size_t len);
BLANG_API void blBufferCut(BlBuffer *b, size_t len);
BLANG_API void blBufferReplaceChar(BlBuffer *b, size_t start, char c, char r);
BLANG_API void blBufferPrepend(BlBuffer *b, const char *str, size_t len);
BLANG_API void blBufferPrependstr(BlBuffer *b, const char *str);
BLANG_API void blBufferAppendChar(BlBuffer *b, char c);
BLANG_API void blBufferClear(BlBuffer *b);

// Once the buffer is pushed on the Blang stack it becomes a String and can't be modified further
// One can reuse the BlBuffer struct by re-initializing it using the blBufferInit method.
BLANG_API void blBufferPush(BlBuffer *b);
BLANG_API void blBufferFree(BlBuffer *b);

#endif