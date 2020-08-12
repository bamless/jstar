/**
 * MIT License
 *
 * Copyright (c) 2020 Fabrizio Pietrucci
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef JSTAR_H
#define JSTAR_H

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include "jstarconf.h"  // IWYU pragma: export

// -----------------------------------------------------------------------------
// J* VM ENTRY POINTS
// -----------------------------------------------------------------------------

// The J* virtual machine
typedef struct JStarVM JStarVM;

typedef enum JStarResult {
    JSR_EVAL_SUCCESS,  // The VM successfully executed the code
    JSR_SYNTAX_ERR,    // A syntax error has been encountered in parsing
    JSR_COMPILE_ERR,   // An error has been encountered during compilation
    JSR_RUNTIME_ERR,   // An unhandled exception has reached the top of the stack
} JStarResult;

// J* error function callback
typedef void (*JStarErrorCB)(const char* file, int line, const char* error);

// Default implementation of error callback that prints the error to stderr
JSTAR_API void jsrPrintErrorCB(const char* file, int line, const char* error);

typedef struct JstarConf {
    size_t stackSize;            // Initial stack size in bytes
    size_t initGC;               // first GC threshold point
    int heapGrowRate;            // The rate at which the heap will grow after a succesful GC
    JStarErrorCB errorCallback;  // Error callback
} JStarConf;

// Retuns a JStarConf initialized with default values
JSTAR_API JStarConf jsrGetConf();

// Allocate a new VM with all the state needed for code execution
JSTAR_API JStarVM* jsrNewVM(JStarConf* conf);
// Free a previously obtained VM along with all the state
JSTAR_API void jsrFreeVM(JStarVM* vm);

// Evaluate J* code in the context of module (or __main__ in jsrEvaluate)
// as top level <main> function.
// VM_EVAL_SUCCSESS will be returned if the execution completed normally
// In case of errors, either VM_SYNTAX_ERR, VM_COMPILE_ERR or VM_RUNTIME_ERR
// will be returned, and all the errors will be printed to stderr.
JSTAR_API JStarResult jsrEvaluate(JStarVM* vm, const char* path, const char* src);
JSTAR_API JStarResult jsrEvaluateModule(JStarVM* vm, const char* path, const char* name,
                                        const char* src);

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
JSTAR_API JStarResult jsrCall(JStarVM* vm, uint8_t argc);
JSTAR_API JStarResult jsrCallMethod(JStarVM* vm, const char* name, uint8_t argc);

// Prints the the stack trace of the exception at slot 'slot'
JSTAR_API void jsrPrintStacktrace(JStarVM* vm, int slot);
// Init the sys.args list with a list of arguments (usually main arguments)
JSTAR_API void jsrInitCommandLineArgs(JStarVM* vm, int argc, const char** argv);
// Add a path to be searched during module imports
JSTAR_API void jsrAddImportPath(JStarVM* vm, const char* path);

// Raises the axception at 'slot'. If the object at 'slot' is not an exception instance it
// raises a type exception
JSTAR_API void jsrRaiseException(JStarVM* vm, int slot);

// Instantiate an exception from "cls" with "err" as an error string and raises
// it, leaving it on top of the stack.
// If "cls" cannot be found in current module a NameException is raised instead.
JSTAR_API void jsrRaise(JStarVM* vm, const char* cls, const char* err, ...);

// -----------------------------------------------------------------------------
// UTILITY FUNCTIONS AND DEFINITIONS
// -----------------------------------------------------------------------------

// The minimum reserved space for the stack when calling a native function
#define JSTAR_MIN_NATIVE_STACK_SZ 20

// Utility macro for declaring/defining a native function
#define JSR_NATIVE(name) bool name(JStarVM* vm)

// Utility macro for raising an exception from a native function.
// It raises the exception and exits signaling the error.
#define JSR_RAISE(vm, cls, err, ...)           \
    do {                                       \
        jsrRaise(vm, cls, err, ##__VA_ARGS__); \
        return false;                          \
    } while(0)

// Main module and core module names
#define JSR_MAIN_MODULE "__main__"
#define JSR_CORE_MODULE "__core__"

// Ensure `needed` slots are available on the stack
JSTAR_API void jsrEnsureStack(JStarVM* vm, size_t needed);

// A C function callable from J*
typedef bool (*JStarNative)(JStarVM* vm);

// Read a whole file. The returned buffer is malloc'd, so the user should free() it when done.
// On error returns NULL and sets errno to the appropriate error.
JSTAR_API char* jsrReadFile(const char* path);

// -----------------------------------------------------------------------------
// NATIVE REGISTRY
// -----------------------------------------------------------------------------

// J* native registry, used to associate names to native pointers in native c extension modules.
typedef struct JStarNativeReg {
    enum { REG_METHOD, REG_FUNCTION, REG_SENTINEL } type;
    union {
        struct {
            const char* cls;
            const char* name;
            JStarNative meth;
        } method;
        struct {
            const char* name;
            JStarNative fun;
        } function;
    } as;
} JStarNativeReg;

#define JSR_REGFUNC(name, func)      {REG_FUNCTION, {.function = {#name, func}}},
#define JSR_REGMETH(cls, name, meth) {REG_METHOD, {.method = {#cls, #name, meth}}},
#define JSR_REGEND                     \
    {                                  \
        REG_SENTINEL, {                \
            .function = { NULL, NULL } \
        }                              \
    }

// -----------------------------------------------------------------------------
// OVERLOADABLE OPERATOR API
// -----------------------------------------------------------------------------

// Check if two objects are the same. Doesn't call __eq__ overload.
JSTAR_API bool jsrRawEquals(JStarVM* vm, int slot1, int slot2);

// Check if two J* values are equal. May call the __eq__ overload.
JSTAR_API bool jsrEquals(JStarVM* vm, int slot1, int slot2);

// Check if a value is of a certain class.
JSTAR_API bool jsrIs(JStarVM* vm, int slot, int classSlot);

// -----------------------------------------------------------------------------
// ITERATOR PROTOCOL API
// -----------------------------------------------------------------------------

// `iterable` is the slot in which the iterable object is sitting and `res` is the slot of the
// result of the last jsrIter call or, if first time calling jsrIter, a slot containing null.
// jsrNext is called to obtain the next element in the iteration. The element will be placed
// on the top of the stack.
JSTAR_API bool jsrIter(JStarVM* vm, int iterable, int res, bool* err);
JSTAR_API bool jsrNext(JStarVM* vm, int iterable, int res);

// Macro that automatically configures the loop to iterate over a J* iterable using jsrIter and
// jsrNext.
// `iter` is the slot of the iterable we want to iterate over and `code` a block used as the body.
// `cleanup` is used as the cleanup code before exiting in case of an error and it is optional.
// Beware that the macro pushes a new value on top of the stack to store the result of jsrIter, so
// negative slot indeces to access previously pushed elements should be offset by one
#define JSR_FOREACH(iter, code, cleanup)         \
    {                                            \
        bool _err = false;                       \
        jsrPushNull(vm);                         \
        while(jsrIter(vm, iter, -1, &_err)) {    \
            if(_err || !jsrNext(vm, iter, -1)) { \
                cleanup;                         \
                return false;                    \
            }                                    \
            code                                 \
        }                                        \
        jsrPop(vm);                              \
    }

// -----------------------------------------------------------------------------
// C TO J* CONVERTING FUNCTIONS
// -----------------------------------------------------------------------------

// The converted value is left on the top of the stack
JSTAR_API void jsrPushNumber(JStarVM* vm, double number);
JSTAR_API void jsrPushBoolean(JStarVM* vm, bool boolean);
JSTAR_API void jsrPushStringSz(JStarVM* vm, const char* string, size_t size);
JSTAR_API void jsrPushString(JStarVM* vm, const char* string);
JSTAR_API void jsrPushBoolean(JStarVM* vm, bool b);
JSTAR_API void jsrPushHandle(JStarVM* vm, void* handle);
JSTAR_API void jsrPushNull(JStarVM* vm);
JSTAR_API void jsrPushList(JStarVM* vm);
JSTAR_API void jsrPushTuple(JStarVM* vm, size_t size);
JSTAR_API void jsrPushTable(JStarVM* vm);
JSTAR_API void jsrPushValue(JStarVM* vm, int slot);
JSTAR_API void* jsrPushUserdata(JStarVM* vm, size_t size, void (*finalize)(void*));
JSTAR_API void jsrPushNative(JStarVM* vm, const char* module, const char* name, JStarNative nat,
                             uint8_t argc);

#define jsrDup(vm) jsrPushValue(vm, -1)

// Pop a value from the top of the stack
JSTAR_API void jsrPop(JStarVM* vm);
// The the top-most used api stack slot
JSTAR_API int jsrTop(JStarVM* vm);

// -----------------------------------------------------------------------------
// J* TO C CONVERTING FUNCTIONS
// -----------------------------------------------------------------------------

JSTAR_API double jsrGetNumber(JStarVM* vm, int slot);
JSTAR_API bool jsrGetBoolean(JStarVM* vm, int slot);
JSTAR_API void* jsrGetHandle(JStarVM* vm, int slot);
JSTAR_API size_t jsrGetStringSz(JStarVM* vm, int slot);

// BEWARE: The returned string is owned by J*
// and thus is garbage collected. Never use this
// buffer outside the native where it was retrieved.
// Also be careful when popping the original ObjString
// from the stack  while retaining this buffer, because
// if a GC occurs and the string is not found to be
// reachable it'll be collected.
JSTAR_API const char* jsrGetString(JStarVM* vm, int slot);

// -----------------------------------------------------------------------------
// LIST MANIPULATION FUNCTIONS
// -----------------------------------------------------------------------------

// These functions do not perfrom bounds checking,
// use jsrCeckIndex first if needed.
JSTAR_API void jsrListAppend(JStarVM* vm, int slot);
JSTAR_API void jsrListInsert(JStarVM* vm, size_t i, int slot);
JSTAR_API void jsrListRemove(JStarVM* vm, size_t i, int slot);
JSTAR_API void jsrListGet(JStarVM* vm, size_t i, int slot);
JSTAR_API size_t jsrListGetLength(JStarVM* vm, int slot);

// -----------------------------------------------------------------------------
// TUPLE MANIPULATION FUNCTIONS
// -----------------------------------------------------------------------------

// These functions do not perfrom bounds checking,
// use jsrCeckIndex first if needed.
JSTAR_API void jsrTupleGet(JStarVM* vm, size_t i, int slot);
JSTAR_API size_t jsrTupleGetLength(JStarVM* vm, int slot);

// -----------------------------------------------------------------------------
// INSTANCE MANIPULATION FUNCTIONS
// -----------------------------------------------------------------------------

// Set the field "name" of the value at "slot" with the value
// on top of the stack. the value is not popped.
// Returns true in case of success, false otherwise leaving an
// exception on top of the stack
JSTAR_API bool jsrSetField(JStarVM* vm, int slot, const char* name);

// Get the field "name" of the value at "slot".
// Returns true in case of success leaving the result on
// top of the stack, false otherwise leaving an exception
// on top of the stack.
JSTAR_API bool jsrGetField(JStarVM* vm, int slot, const char* name);

// -----------------------------------------------------------------------------
// MODULE MANIPULATION FUNCTIONS
// -----------------------------------------------------------------------------

// Set the global "name" of the module "mname" with the value
// on top of the stack. the value is not popped.
// If calling inside a native function module can be NULL, and
// the used module will be the current one
JSTAR_API void jsrSetGlobal(JStarVM* vm, const char* module, const char* name);

// Get the global "name" of the module "mname".
// Returns true in case of success leaving the result on the
// top of the stack, false otherwise leaving an exception on
// top of the stack.
// If calling inside a native function module can be NULL, and
// the used module will be the current one
JSTAR_API bool jsrGetGlobal(JStarVM* vm, const char* module, const char* name);

// -----------------------------------------------------------------------------
// CLASS MANIPULATION FUNCTIONS
// -----------------------------------------------------------------------------

JSTAR_API void jsrBindNative(JStarVM* vm, int clsSlot, int natSlot);

// -----------------------------------------------------------------------------
// USERDATA MANIPULATION FUNCTIONS
// -----------------------------------------------------------------------------

JSTAR_API void* jsrGetUserdata(JStarVM* vm, int slot);

// -----------------------------------------------------------------------------
// TYPE CHECKING FUNCTIONS
// -----------------------------------------------------------------------------

// These functions return true if the slot is of the given type, false otherwise
JSTAR_API bool jsrIsNumber(JStarVM* vm, int slot);
JSTAR_API bool jsrIsInteger(JStarVM* vm, int slot);
JSTAR_API bool jsrIsString(JStarVM* vm, int slot);
JSTAR_API bool jsrIsList(JStarVM* vm, int slot);
JSTAR_API bool jsrIsTuple(JStarVM* vm, int slot);
JSTAR_API bool jsrIsBoolean(JStarVM* vm, int slot);
JSTAR_API bool jsrIsHandle(JStarVM* vm, int slot);
JSTAR_API bool jsrIsNull(JStarVM* vm, int slot);
JSTAR_API bool jsrIsInstance(JStarVM* vm, int slot);
JSTAR_API bool jsrIsTable(JStarVM* vm, int slot);
JSTAR_API bool jsrIsFunction(JStarVM* vm, int slot);
JSTAR_API bool jsrIsUserdata(JStarVM* vm, int slot);

// These functions return true if the slot is of the given type, false otherwise
// leaving a TypeException on top of the stack with a message customized with 'name'
JSTAR_API bool jsrCheckNumber(JStarVM* vm, int slot, const char* name);
JSTAR_API bool jsrCheckInt(JStarVM* vm, int slot, const char* name);
JSTAR_API bool jsrCheckString(JStarVM* vm, int slot, const char* name);
JSTAR_API bool jsrCheckList(JStarVM* vm, int slot, const char* name);
JSTAR_API bool jsrCheckTuple(JStarVM* vm, int slot, const char* name);
JSTAR_API bool jsrCheckBoolean(JStarVM* vm, int slot, const char* name);
JSTAR_API bool jsrCheckInstance(JStarVM* vm, int slot, const char* name);
JSTAR_API bool jsrCheckHandle(JStarVM* vm, int slot, const char* name);
JSTAR_API bool jsrCheckTable(JStarVM* vm, int slot, const char* name);
JSTAR_API bool jsrCheckFunction(JStarVM* vm, int slot, const char* name);
JSTAR_API bool jsrCheckUserdata(JStarVM* vm, int slot, const char* name);

// Utility macro for checking a value type in the stack.
// In case of error it exits signaling the error
#define JSR_CHECK(type, slot, name) \
    if(!jsrCheck##type(vm, slot, name)) return false

// Check if the value at slot "slot" is an integer >= 0 and < max.
// Returns the number casted to size_t if true, SIZE_MAX if false
// leaving an exception on top of the stack.
JSTAR_API size_t jsrCheckIndex(JStarVM* vm, int slot, size_t max, const char* name);
// Check if the provided double 'num' is an integer >= 0 and < max.
// Returns the number casted to size_t if true, SIZE_MAX if false
// leaving an exception on top of the stack.
JSTAR_API size_t jsrCheckIndexNum(JStarVM* vm, double num, size_t max);

// -----------------------------------------------------------------------------
// JSTARBUFFER API
// -----------------------------------------------------------------------------

// Dynamic Buffer that holds memory allocated by the J* garbage collector.
// This memory is owned by J*, but cannot be collected until the buffer
// is pushed on the stack using the jsrBufferPush method.
// Used for efficient creation of Strings in the native API.
typedef struct JStarBuffer {
    JStarVM* vm;
    size_t size;
    size_t len;
    char* data;
} JStarBuffer;

JSTAR_API void jsrBufferInit(JStarVM* vm, JStarBuffer* b);
JSTAR_API void jsrBufferInitSz(JStarVM* vm, JStarBuffer* b, size_t size);
JSTAR_API void jsrBufferAppend(JStarBuffer* b, const char* str, size_t len);
JSTAR_API void jsrBufferAppendstr(JStarBuffer* b, const char* str);
JSTAR_API void jsrBufferAppendvf(JStarBuffer* b, const char* fmt, va_list ap);
JSTAR_API void jsrBufferAppendf(JStarBuffer* b, const char* fmt, ...);
JSTAR_API void jsrBufferTrunc(JStarBuffer* b, size_t len);
JSTAR_API void jsrBufferCut(JStarBuffer* b, size_t len);
JSTAR_API void jsrBufferReplaceChar(JStarBuffer* b, size_t start, char c, char r);
JSTAR_API void jsrBufferPrepend(JStarBuffer* b, const char* str, size_t len);
JSTAR_API void jsrBufferPrependstr(JStarBuffer* b, const char* str);
JSTAR_API void jsrBufferAppendChar(JStarBuffer* b, char c);
JSTAR_API void jsrBufferClear(JStarBuffer* b);

// Once the buffer is pushed on the J* stack it becomes a String and can't be modified further
// One can reuse the JStarBuffer struct by re-initializing it using the jsrBufferInit method.
JSTAR_API void jsrBufferPush(JStarBuffer* b);
JSTAR_API void jsrBufferFree(JStarBuffer* b);

#endif