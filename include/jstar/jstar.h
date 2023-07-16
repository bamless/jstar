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

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include "buffer.h"  // IWYU pragma: export
#include "conf.h"    // IWYU pragma: export

// -----------------------------------------------------------------------------
// FORWARD DECLARATIONS
// -----------------------------------------------------------------------------

// The J* virtual machine
typedef struct JStarVM JStarVM;

// J* native registry
typedef struct JStarNativeReg JStarNativeReg;

// A C function callable from J*
typedef bool (*JStarNative)(JStarVM* vm);

// Generic error code used by several J* API functions
typedef enum JStarResult {
    JSR_SUCCESS,          // The VM successfully executed the code
    JSR_SYNTAX_ERR,       // A syntax error has been encountered in parsing
    JSR_COMPILE_ERR,      // An error has been encountered during compilation
    JSR_RUNTIME_ERR,      // An unhandled exception has reached the top of the stack
    JSR_DESERIALIZE_ERR,  // An error occurred during deserialization of compiled code
    JSR_VERSION_ERR,      // Incompatible version of compiled code
} JStarResult;

// Import result struct, contains a resolved module's code and native registry
typedef struct JStarImportResult {
    const char* code;         // The resolved module code (source or binary)
    size_t codeLength;        // Length of the code field
    const char* path;         // The resolved module path (can be fictitious)
    JStarNativeReg* reg;      // Resolved native registry for the module (can be NULL)
    void (*finalize)(void*);  // Finalization callback. Called after a resolved import (can be NULL)
    void* userData;           // Custom user data passed to the finalization function (can be NULL)
} JStarImportResult;

// -----------------------------------------------------------------------------
// HOOKS AND CALLBAKCS
// -----------------------------------------------------------------------------

// J* import callback, invoked when executing `import`s in the VM
typedef JStarImportResult (*JStarImportCB)(JStarVM* vm, const char* moduleName);

// J* error function callback. Invoked when syntax, compilation, dederializtion
// or syntax errors are encountered.
typedef void (*JStarErrorCB)(JStarVM* vm, JStarResult err, const char* file, int line,
                             const char* error);

// Default implementation of the error callback that prints all errors to stderr
JSTAR_API void jsrPrintErrorCB(JStarVM* vm, JStarResult err, const char* file, int line,
                               const char* error);

// -----------------------------------------------------------------------------
// J* VM INITIALIZATION
// -----------------------------------------------------------------------------

// Struct that holds the J* vm configuration options
typedef struct JstarConf {
    size_t startingStackSize;       // Initial stack size in bytes
    size_t firstGCCollectionPoint;  // first GC collection point in bytes
    int heapGrowRate;               // The rate at which the heap will grow after a GC pass
    JStarErrorCB errorCallback;     // Error callback
    JStarImportCB importCallback;   // Import callback (can be NULL)
    void* customData;               // Custom data associated with the VM (can be NULL)
} JStarConf;

// Retuns a JStarConf struct initialized with default values
JSTAR_API JStarConf jsrGetConf(void);

// Allocates a new VM with all the state needed for code execution. Does not initialize runtime
JSTAR_API JStarVM* jsrNewVM(const JStarConf* conf);

// Inits the J* runtime, including the core and main module. Must be called prior to executing code
JSTAR_API void jsrInitRuntime(JStarVM* vm);

// Free a previously obtained VM along with all of its state
JSTAR_API void jsrFreeVM(JStarVM* vm);

// Inits the argv list with a list of arguments (usually main arguments). Must be called after
// runtime initialization
JSTAR_API void jsrInitCommandLineArgs(JStarVM* vm, int argc, const char** argv);

// Gets the custom data associated with the VM at configuration time (if any)
JSTAR_API void* jsrGetCustomData(JStarVM* vm);

// Breaks J* evaluation at the first chance possible. This function is signal-handler safe.
JSTAR_API void jsrEvalBreak(JStarVM* vm);

// -----------------------------------------------------------------------------
// CODE EXECUTION
// -----------------------------------------------------------------------------

// Evaluate J* code read with `jsrReadFile` in the context of module (or __main__ in jsrEval).
// JSR_SUCCESS will be returned if the execution completed normally.
// In case of errors, either JSR_SYNTAX_ERR, JSR_COMPILE_ERR, _JSR_DESERIALIZE_ERR or JSR_VER_ERR
// will be returned.
// All errors will be forwared to the error callback as well.
// The `path` argument is the file path that will passed to the forward callback on errors
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

// -----------------------------------------------------------------------------
// C TO J* VALUE CONVERSION API
// -----------------------------------------------------------------------------

// The converted value is left on the top of the stack
JSTAR_API void jsrPushNumber(JStarVM* vm, double number);
JSTAR_API void jsrPushBoolean(JStarVM* vm, bool boolean);
JSTAR_API void jsrPushStringSz(JStarVM* vm, const char* string, size_t size);
JSTAR_API void jsrPushString(JStarVM* vm, const char* string);
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

// Pop n values from the top of the stack
JSTAR_API void jsrPopN(JStarVM* vm, int n);

// The the top-most used api stack slot
JSTAR_API int jsrTop(JStarVM* vm);

// -----------------------------------------------------------------------------
// J* TO C VALUE CONVERSION API
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
// OPERATOR API
// -----------------------------------------------------------------------------

// Check if two objects are the same. Doesn't call __eq__ overload.
// Returns true if the two objects are the same
JSTAR_API bool jsrRawEquals(JStarVM* vm, int slot1, int slot2);

// Check if two J* values are equal. May call the __eq__ overload.
// Returns true if the two objects are the same, as defined by the __eq__ method
JSTAR_API bool jsrEquals(JStarVM* vm, int slot1, int slot2);

// Check if a value is of a certain class.
// Returns true if the object is an instance of the class, false if it's not
// or if `classSlot` doesn't point to a Class object
JSTAR_API bool jsrIs(JStarVM* vm, int slot, int classSlot);

// -----------------------------------------------------------------------------
// EXCEPTION API
// -----------------------------------------------------------------------------

// Prints the the stacktrace of the exception at slot 'slot'. If the value at 'slot' is not an
// Exception, it doesn't print anything ad returns successfully
JSTAR_API void jsrPrintStacktrace(JStarVM* vm, int slot);

// Get the stacktrace of the exception at 'slot' and leaves it on top of the stack.
// The stacktrace is a formatted String containing the traceback and the exception error.
// If the value at slot is not an Exception, it places An empty String on top of the stack
// and returns succesfully
JSTAR_API void jsrGetStacktrace(JStarVM* vm, int slot);

// Raises the axception at 'slot'. If the object at 'slot' is not an exception instance it
// raises a TypeException instead
JSTAR_API void jsrRaiseException(JStarVM* vm, int slot);

// Instantiate an exception from "cls" with "err" as an error string and raises
// it, leaving it on top of the stack.
// If "cls" cannot be found in current module a NameException is raised instead.
JSTAR_API void jsrRaise(JStarVM* vm, const char* cls, const char* err, ...);

// -----------------------------------------------------------------------------
// LIST API
// -----------------------------------------------------------------------------

// These functions do not perfrom bounds checking, use jsrCheckIndex/Num first if needed.
JSTAR_API void jsrListAppend(JStarVM* vm, int slot);
JSTAR_API void jsrListInsert(JStarVM* vm, size_t i, int slot);
JSTAR_API void jsrListRemove(JStarVM* vm, size_t i, int slot);
JSTAR_API void jsrListGet(JStarVM* vm, size_t i, int slot);
JSTAR_API size_t jsrListGetLength(JStarVM* vm, int slot);

// -----------------------------------------------------------------------------
// TUPLE API
// -----------------------------------------------------------------------------

// These functions do not perfrom bounds checking, use jsrCheckIndex/Num first if needed.
JSTAR_API void jsrTupleGet(JStarVM* vm, size_t i, int slot);
JSTAR_API size_t jsrTupleGetLength(JStarVM* vm, int slot);

// -----------------------------------------------------------------------------
// ITERATOR API
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
// SEQUENCE API
// -----------------------------------------------------------------------------

// These functions are similar to jsrTupleGet, jsrListGet/jsrListSet and jsrList/jsrTupleGetLength
// above, but operate on generic types. Uslually working with the former is more convinient, as
// some operations cannot fail when applied on Tuples or Lists and therefore require less error
// checking and less stack manipulation. Nonetheless, if you require operating on heterogeneous
// types that may overload __get__ and __set__, you should use these.
// Also, they perform bounds checking automatically.

// Returns the result of subscritping the value at `slot` with the value on top of the stack
// In case of success returns true and the result of the operation is placed on top of the stack
// In case of errors returns false and an exception is placed on top of the stack
JSTAR_API bool jsrSubscriptGet(JStarVM* vm, int slot);

// Subscript-assign the value at `slot` with the two values on top of the stack
// The top-most value will be interpreted as the `value` and the second top-most as the `key`
// In case of success returns true and the assigned `value` will be left on top of the stack
// In case of errors returns false and an exception is placed on top of the stack
JSTAR_API bool jsrSubscriptSet(JStarVM* vm, int slot);

// Returns the size of the value at `slot`
// In case of success returns the size
// In case of error returns SIZE_MAX and an exception is placed on top of the stack
JSTAR_API size_t jsrGetLength(JStarVM* vm, int slot);

// -----------------------------------------------------------------------------
// INSTANCE API
// -----------------------------------------------------------------------------

// Set the field `name` of the value at `slot` with the value on top of the stack
// The value is not popped
// Returns true in case of success
// Returns false otherwise leaving an exception on top of the stack
JSTAR_API bool jsrSetField(JStarVM* vm, int slot, const char* name);

// Get the field `name` of the value at `slot`
// Returns true in case of success leaving the result on top of the stack
// Returns false otherwise leaving an exception on top of the stack.
JSTAR_API bool jsrGetField(JStarVM* vm, int slot, const char* name);

// -----------------------------------------------------------------------------
// MODULE API
// -----------------------------------------------------------------------------

// Set the global `name` of the module `module` with the value on top of the stack
// The value is not popped
// If calling inside a native "module" can be NULL, and the used module will be the current one
JSTAR_API void jsrSetGlobal(JStarVM* vm, const char* module, const char* name);

// Get the global `name` of the module `module`
// Returns true in case of success leaving the result on the top of the stack
// Returns false otherwise leaving an exception on top of the stack
// If calling inside a native "module" can be NULL, and the used module will be the current one
JSTAR_API bool jsrGetGlobal(JStarVM* vm, const char* module, const char* name);

// -----------------------------------------------------------------------------
// CLASS API
// -----------------------------------------------------------------------------

// Binds the native at `natSlot` to the class at `clsSlot`
// Does not perform type checking, the user must ensure `clsSlot` is indeed a Class and `natSlot`
// a Native
JSTAR_API void jsrBindNative(JStarVM* vm, int clsSlot, int natSlot);

// -----------------------------------------------------------------------------
// USERDATA API
// -----------------------------------------------------------------------------

// Get the memory associated with the UserDatum at `slot`
// Does not perform type checking, the user must ensure `slot` is a Userdatum
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
// leaving a TypeException on top of the stack with a message customized with `name`
JSTAR_API bool jsrCheckNumber(JStarVM* vm, int slot, const char* name);
JSTAR_API bool jsrCheckInt(JStarVM* vm, int slot, const char* name);
JSTAR_API bool jsrCheckString(JStarVM* vm, int slot, const char* name);
JSTAR_API bool jsrCheckList(JStarVM* vm, int slot, const char* name);
JSTAR_API bool jsrCheckTuple(JStarVM* vm, int slot, const char* name);
JSTAR_API bool jsrCheckBoolean(JStarVM* vm, int slot, const char* name);
JSTAR_API bool jsrCheckNull(JStarVM* vm, int slot, const char* name);
JSTAR_API bool jsrCheckInstance(JStarVM* vm, int slot, const char* name);
JSTAR_API bool jsrCheckHandle(JStarVM* vm, int slot, const char* name);
JSTAR_API bool jsrCheckTable(JStarVM* vm, int slot, const char* name);
JSTAR_API bool jsrCheckFunction(JStarVM* vm, int slot, const char* name);
JSTAR_API bool jsrCheckUserdata(JStarVM* vm, int slot, const char* name);

// Utility macro for checking a value type in the stack.
// In case of error it exits signaling the error
#define JSR_CHECK(type, slot, name) \
    if(!jsrCheck##type(vm, slot, name)) return false

// Check if the value at slot `slot` is an integer >= 0 and < max.
// Returns the number casted to size_t if true, SIZE_MAX if false
// leaving an exception on top of the stack.
JSTAR_API size_t jsrCheckIndex(JStarVM* vm, int slot, size_t max, const char* name);

// Check if the provided double `num` is an integer >= 0 and < max.
// Returns the number casted to size_t if true, SIZE_MAX if false
// leaving an exception on top of the stack.
JSTAR_API size_t jsrCheckIndexNum(JStarVM* vm, double num, size_t max);

// -----------------------------------------------------------------------------
// NATIVES AND NATIVE REGISTRATION
// -----------------------------------------------------------------------------

// Main module and core module names
#define JSR_CONSTRUCT   "@construct"
#define JSR_MAIN_MODULE "__main__"
#define JSR_CORE_MODULE "__core__"

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

// Ensure `needed` slots are available on the stack
JSTAR_API void jsrEnsureStack(JStarVM* vm, size_t needed);

// J* native registry, used to associate names to c function
// pointers during native resolution after module import.
struct JStarNativeReg {
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
};

// Macros to simplify native registry creation
#define JSR_REGFUNC(name, func)      {REG_FUNCTION, {.function = {#name, func}}},
#define JSR_REGMETH(cls, name, meth) {REG_METHOD, {.method = {#cls, #name, meth}}},
#define JSR_REGEND                     \
    {                                  \
        REG_SENTINEL, {                \
            .function = { NULL, NULL } \
        }                              \
    }

// -----------------------------------------------------------------------------
// CODE COMPILATION
// -----------------------------------------------------------------------------

/*
 * The following functions are safe to call without initializing the runtime,
 * as they only deal in code compilation or disassembly
 */

// Compiles the provided source to bytecode, placing the result in `out`.
// JSR_SUCCESS will be returned if the compilation completed normally.
// In case of errors either JSR_SYNTAX_ERR or JSR_COMPILE_ERR will be returned.
// All errors will be forwarded to the error callback as well.
// The `path` argument is the file path that will passed to the forward callback on errors
// Please note that the vm always compiles source code before execution, so using this function
// just to immediately call jsrEval on the result is useless and less efficient than directly
// calling jsrEvalString. Its intended use is to compile some code to later store it on file, send
// it over the network, etc...
JSTAR_API JStarResult jsrCompileCode(JStarVM* vm, const char* path, const char* src,
                                     JStarBuffer* out);

// Disassembles the bytecode provided in `code` and prints it to stdout
// The `path` argument is the file path that will passed to the forward callback on errors
// Prints nothing if the provided `code` buffer doesn't contain valid bytecode
JSTAR_API JStarResult jsrDisassembleCode(JStarVM* vm, const char* path, const JStarBuffer* code);

// Reads a J* source or compiled file, placing the output in out.
// Returns true on success, false on error setting errno to the approriate value.
// Tipically used alongside jsrEval to execute a J* source or compiled file.
JSTAR_API bool jsrReadFile(JStarVM* vm, const char* path, JStarBuffer* out);

#endif
