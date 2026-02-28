#ifndef IMPORT_H
#define IMPORT_H

#include <stddef.h>

#include "jstar.h"
#include "object.h"
#include "parse/ast.h"

/**
 * The import system is responsible for loading and compiling J* modules.
 * This files defines three sets of functions:
 *  - Functions for compiling/deserializing a module from source code or bytecode
 *  - Functions for managing the module cache
 *  - `importModule` which kickstarts the import process of the VM
 */

// Compile a module from source code.
// A module with name 'name' is created in the cache if it doesn't already exist.
// Returns a function representing the module's 'main' (top-level scope). Call this function to
// initialize the module.
// On error, returns NULL.
ObjFunction* compileModule(JStarVM* vm, const char* path, ObjString* name, JStarStmt* program);

// Similar to the above, but deserialize a module from bytecode.
// On success, returns JSR_SUCCESS and sets 'out' to the deserialized function.
// On error, returns an error code and leaves out unchanged.
JStarResult deserializeModule(JStarVM* vm, const char* path, ObjString* name, const void* code,
                              size_t len, ObjFunction** out);

// Sets a module in the cache.
void setModule(JStarVM* vm, ObjString* name, ObjModule* module);

// Retrieves a module from the cache.
ObjModule* getModule(JStarVM* vm, ObjString* name);

// Import a module by name.
//
// Calls the user provided import callback to resolve the module.
// If the module ins't present in the cache, the module's main function is left on top of the stack.
// Otherwise, `NULL_VAL` is left on top of the stack, signaling that the module has already been
// imported.
//
// Returns the module object on success, NULL on error.
ObjModule* importModule(JStarVM* vm, ObjString* name);

#endif
