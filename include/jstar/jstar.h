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

// J* dynamic buffer
typedef struct JStarBuffer JStarBuffer;

typedef enum JStarResult {
    JSR_SUCCESS,          // The VM successfully executed the code
    JSR_SYNTAX_ERR,       // A syntax error has been encountered in parsing
    JSR_COMPILE_ERR,      // An error has been encountered during compilation
    JSR_RUNTIME_ERR,      // An unhandled exception has reached the top of the stack
    JSR_DESERIALIZE_ERR,  // An error occurred during deserialization of compiled code
    JSR_VERSION_ERR,      // Incompatible version of compiled code
} JStarResult;

// J* error function callback. Called when syntax, compilation or dederializtion errors are
// encountered.
// 'file' is the path of the file that caused the error, 'line' the line where the error occurred
// (or -1 of not line information is available), and 'error' is a descriptive message of the error.
typedef void (*JStarErrorCB)(const char* file, int line, const char* error);

// Default implementation of the error callback that prints all errors to stderr
JSTAR_API void jsrPrintErrorCB(const char* file, int line, const char* error);

typedef struct JstarConf {
    size_t stackSize;            // Initial stack size in bytes
    size_t initGC;               // first GC threshold point
    int heapGrowRate;            // The rate at which the heap will grow after a succesful GC
    JStarErrorCB errorCallback;  // Error callback
    void* customData;            // Custom data associated with the VM
} JStarConf;

// Retuns a JStarConf initialized with default values
JSTAR_API JStarConf jsrGetConf(void);

// Allocate a new VM with all the state needed for code execution
JSTAR_API JStarVM* jsrNewVM(const JStarConf* conf);

// Free a previously obtained VM along with all of its state
JSTAR_API void jsrFreeVM(JStarVM* vm);

// Get the custom data associated with the VM at configuration time (if any)
void* jsrGetCustomData(JStarVM* vm);

// Evaluate J* code read with `jsrReadFile` in the context of module (or __main__ in jsrEval).
// JSR_SUCCESS will be returned if the execution completed normally.
// In case of errors, either JSR_SYNTAX_ERR, JSR_COMPILE_ERR, _JSR_DESERIALIZE_ERR or JSR_VER_ERR
// will be returned.
// In the case of a JSR_RUNTIME_ERR the stacktrace of the Exception will be printed to stderr.
// All other errors will be forwarded to the error callback
JSTAR_API JStarResult jsrEval(JStarVM* vm, const char* path, const JStarBuffer* code);
JSTAR_API JStarResult jsrEvalModule(JStarVM* vm, const char* path, const char* module,
                                    const JStarBuffer* code);

// Similar to the `jsrEval` family of functions, but takes in a c string of the J* source code to
// evaluate.
// The provided string can be any c string, and it doesn't have to be the result of a call
// to `jsrReadFile`. This means that these functions cannot execute compiled J* code, only source
JSTAR_API JStarResult jsrEvalString(JStarVM* vm, const char* path, const char* src);
JSTAR_API JStarResult jsrEvalModuleString(JStarVM* vm, const char* path, const char* module,
                                          const char* src);

// Compiles the provided source to bytecode, placing the result in `out`.
// JSR_SUCCESS will be returned if the compilation completed normally.
// In case of errors either JSR_SYNTAX_ERR or JSR_COMPILE_ERR will be returned.
// Please note that the vm always compiles source code before execution, so using this function
// just to immediately call jsrEval on the result is useless and less efficient than directly
// calling jsrEvalString. Its intended use is to compile some code to later store it on file, send
// it over the network, etc...
JSTAR_API JStarResult jsrCompileCode(JStarVM* vm, const char* path, const char* src,
                                     JStarBuffer* out);

// Reads a J* source or compiled file, placing the output in out.
// Returns true on success, false on error setting errno to the approriate value.
// Tipically used alongside jsrEval to execute a J* source or compiled file.
JSTAR_API bool jsrReadFile(JStarVM* vm, const char* path, JStarBuffer* out);

// Call any callable object (typically a function) that sits on the top of the stack along with its
// arguments.
// The state of the stack when calling should be:
//
//  ... [callable][arg1][arg2]...[argn] $top
//          |      |_________________|
//          |               |
//        object        arguments
//
// In case of success JSR_SUCCSESS will be returned, and the result will be placed on the top of the
// stack in place of the callable object. The state of the stack after a succesful call will be:
//
//  ... [result] $top [arg1][arg2] ... [argn] <-- the arguments are popped
//
// If an exception has been raised instead, JSR_RUNTIME_ERR will be returned and the exception will
// be placed in place of the callable object as a result.
JSTAR_API JStarResult jsrCall(JStarVM* vm, uint8_t argc);

// Similar to the above, but tries to call a method called `name` on an object.
JSTAR_API JStarResult jsrCallMethod(JStarVM* vm, const char* name, uint8_t argc);

// Breaks J* evaluation at the first chance possible. This function is signal-handler safe.
JSTAR_API void jsrEvalBreak(JStarVM* vm);

// Prints the the stacktrace of the exception at slot 'slot'. If the value at 'slot' is not an
// Exception, or is a non-yet-raised Exception, it doesn't print anything ad returns successfully
JSTAR_API void jsrPrintStacktrace(JStarVM* vm, int slot);

// Get the stacktrace of the exception at 'slot' and leaves it on top of the stack.
// The stacktrace is a formatted String containing the traceback and the exception error.
// If the value at slot is not an Exception, or is a non-yet-raised Exception, it places
// An empty String on top of the stack and returns succesfully
JSTAR_API void jsrGetStacktrace(JStarVM* vm, int slot);

// Init the sys.args list with a list of arguments (usually main arguments)
JSTAR_API void jsrInitCommandLineArgs(JStarVM* vm, int argc, const char** argv);

// Add a path to be searched during module imports
JSTAR_API void jsrAddImportPath(JStarVM* vm, const char* path);

// Raises the axception at 'slot'. If the object at 'slot' is not an exception instance it
// raises a TypeException instead
JSTAR_API void jsrRaiseException(JStarVM* vm, int slot);

// Instantiate an exception from "cls" with "err" as an error string and raises
// it, leaving it on top of the stack.
// If "cls" cannot be found in current module a NameException is raised instead.
JSTAR_API void jsrRaise(JStarVM* vm, const char* cls, const char* err, ...);

// -----------------------------------------------------------------------------
// UTILITY FUNCTIONS AND DEFINITIONS
// -----------------------------------------------------------------------------

// The guaranteed stack space available in a native function call.
// Use jsrEnusreStack if you need more
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
        jsrEnsureStack(vm, 2);                   \
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
// Can be used for efficient creation of Strings in the native API.
struct JStarBuffer {
    JStarVM* vm;
    size_t capacity, size;
    char* data;
};

JSTAR_API void jsrBufferInit(JStarVM* vm, JStarBuffer* b);
JSTAR_API void jsrBufferInitCapacity(JStarVM* vm, JStarBuffer* b, size_t capacity);
JSTAR_API void jsrBufferAppend(JStarBuffer* b, const char* str, size_t len);
JSTAR_API void jsrBufferAppendStr(JStarBuffer* b, const char* str);
JSTAR_API void jsrBufferAppendvf(JStarBuffer* b, const char* fmt, va_list ap);
JSTAR_API void jsrBufferAppendf(JStarBuffer* b, const char* fmt, ...);
JSTAR_API void jsrBufferTrunc(JStarBuffer* b, size_t len);
JSTAR_API void jsrBufferCut(JStarBuffer* b, size_t len);
JSTAR_API void jsrBufferReplaceChar(JStarBuffer* b, size_t start, char c, char r);
JSTAR_API void jsrBufferPrepend(JStarBuffer* b, const char* str, size_t len);
JSTAR_API void jsrBufferPrependStr(JStarBuffer* b, const char* str);
JSTAR_API void jsrBufferAppendChar(JStarBuffer* b, char c);
JSTAR_API void jsrBufferShrinkToFit(JStarBuffer* b);
JSTAR_API void jsrBufferClear(JStarBuffer* b);

// Once the buffer is pushed on the J* stack it becomes a String and can't be modified further
// One can reuse the JStarBuffer struct by re-initializing it using the jsrBufferInit method.
JSTAR_API void jsrBufferPush(JStarBuffer* b);

// If not pushed with jsrBufferPush the buffer must be freed
JSTAR_API void jsrBufferFree(JStarBuffer* b);

#endif