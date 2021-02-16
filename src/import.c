#include "import.h"

#include <stdio.h>
#include <string.h>

#include "compiler.h"
#include "const.h"
#include "dynload.h"
#include "hashtable.h"
#include "jstar.h"
#include "parse/parser.h"
#include "serialize.h"
#include "std/modules.h"
#include "util.h"
#include "value.h"
#include "vm.h"

static ObjModule* getOrCreateModule(JStarVM* vm, ObjString* name) {
    ObjModule* module = getModule(vm, name);

    if(module == NULL) {
        push(vm, OBJ_VAL(name));
        module = newModule(vm, name);
        pop(vm);

        hashTableImportNames(&module->globals, &vm->core->globals);  // implicitly import core names
        setModule(vm, name, module);
    }

    return module;
}

ObjFunction* compileWithModule(JStarVM* vm, const char* file, ObjString* name, JStarStmt* program) {
    ObjModule* module = getOrCreateModule(vm, name);
    if(program != NULL) {
        ObjFunction* fn = compile(vm, file, module, program);
        return fn;
    }
    return NULL;
}

ObjFunction* deserializeWithModule(JStarVM* vm, const char* path, ObjString* name,
                                   const JStarBuffer* code, JStarResult* err) {
    ObjModule* module = getOrCreateModule(vm, name);
    ObjFunction* fn = deserialize(vm, module, code, err);

    // TODO: Is this the best place to forward errors to the callback?
    // Considering how `compileWithModule` already forwards the errors, and how this is supposed to
    // be its `binary` counterpart I'd say yes, but I'm not 100% convinced yet. 
    // Anyway, for now this works well, better than the previous solution
    if(*err == JSR_VERSION_ERR) {
        reportError(vm, *err, path, -1, "Incompatible binary file version");
    }

    if(*err == JSR_DESERIALIZE_ERR) {
        reportError(vm, *err, path, -1, "Malformed binary file");
    }

    return fn;
}

static void setModuleInParent(JStarVM* vm, ObjModule* mod) {
    ObjString* name = mod->name;
    const char* lastDot = strrchr(name->data, '.');
    if(lastDot == NULL) return;  // Not a submodule, nothing to do

    const char* simpleName = lastDot + 1;
    ObjModule* parent = getModule(vm, copyString(vm, name->data, simpleName - name->data - 1));
    ASSERT(parent, "Submodule parent could not be found.");
    hashTablePut(&parent->globals, copyString(vm, simpleName, strlen(simpleName)), OBJ_VAL(mod));
}

void setModule(JStarVM* vm, ObjString* name, ObjModule* module) {
    hashTablePut(&vm->modules, name, OBJ_VAL(module));
    hashTablePut(&module->globals, copyString(vm, "__name__", 8), OBJ_VAL(name));
    setModuleInParent(vm, module);
}

ObjModule* getModule(JStarVM* vm, ObjString* name) {
    Value module;
    if(!hashTableGet(&vm->modules, name, &module)) {
        return NULL;
    }
    return AS_MODULE(module);
}

static void tryNativeExtension(JStarVM* vm, JStarBuffer* modulePath, ObjString* moduleName) {
    const char* moduleDir = strrchr(modulePath->data, '/');
    const char* lastDot = strrchr(moduleName->data, '.');
    const char* simpleName = lastDot ? lastDot + 1 : moduleName->data;

    jsrBufferTrunc(modulePath, moduleDir - modulePath->data);
    jsrBufferAppendf(modulePath, "/" DL_PREFIX "%s" DL_SUFFIX, simpleName);

    void* dynlib = dynload(modulePath->data);
    if(dynlib != NULL) {
        // Reuse modulepath to create open function name
        jsrBufferClear(modulePath);
        jsrBufferAppendf(modulePath, "jsr_open_%s", simpleName);

        JStarNativeReg* (*registry)(void) = dynsim(dynlib, modulePath->data);
        if(registry == NULL) {
            dynfree(dynlib);
            return;
        }

        ObjModule* m = getModule(vm, moduleName);
        m->natives.dynlib = dynlib;
        m->natives.registry = (*registry)();
    }
}

static bool importSource(JStarVM* vm, const char* path, ObjString* name, const char* source) {
    JStarStmt* program = jsrParse(path, source, parseErrorCallback, vm);
    if(program == NULL) {
        return false;
    }

    ObjFunction* moduleFun = compileWithModule(vm, path, name, program);
    jsrStmtFree(program);

    if(moduleFun == NULL) {
        return false;
    }

    push(vm, OBJ_VAL(moduleFun));
    vm->sp[-1] = OBJ_VAL(newClosure(vm, moduleFun));

    return true;
}

static bool importBinary(JStarVM* vm, const char* path, ObjString* name, const JStarBuffer* code) {
    JStarResult res;
    ObjFunction* moduleFun = deserializeWithModule(vm, path, name, code, &res);
    if(res != JSR_SUCCESS) {
        return false;
    }

    push(vm, OBJ_VAL(moduleFun));
    vm->sp[-1] = OBJ_VAL(newClosure(vm, moduleFun));

    return true;
}

typedef enum ImportResult {
    IMPORT_OK,
    IMPORT_ERR,
    IMPORT_NOT_FOUND,
} ImportResult;

static ImportResult importFromPath(JStarVM* vm, JStarBuffer* path, ObjString* name) {
    JStarBuffer src;
    if(!jsrReadFile(vm, path->data, &src)) {
        return IMPORT_NOT_FOUND;
    }

    bool res;
    if(isCompiledCode(&src)) {
        res = importBinary(vm, path->data, name, &src);
    } else {
        res = importSource(vm, path->data, name, src.data);
    }

    jsrBufferFree(&src);
    if(!res) return IMPORT_ERR;

    tryNativeExtension(vm, path, name);
    return IMPORT_OK;
}

static bool importModuleOrPackage(JStarVM* vm, ObjString* name) {
    ObjList* paths = vm->importPaths;

    JStarBuffer fullPath;
    jsrBufferInit(vm, &fullPath);

    for(size_t i = 0; i < paths->count + 1; i++) {
        if(i < paths->count) {
            if(!IS_STRING(paths->arr[i])) continue;
            jsrBufferAppendStr(&fullPath, AS_STRING(paths->arr[i])->data);
            if(fullPath.size > 0 && fullPath.data[fullPath.size - 1] != '/') {
                jsrBufferAppendChar(&fullPath, '/');
            }
        }

        size_t moduleStart = fullPath.size;
        size_t moduleEnd = moduleStart + name->length;
        jsrBufferAppendStr(&fullPath, name->data);
        jsrBufferReplaceChar(&fullPath, moduleStart, '.', '/');

        ImportResult res;

        // Try to load a binary package (__package__.jsc file in a directory)
        jsrBufferAppendStr(&fullPath, "/" PACKAGE_FILE JSC_EXT);

        res = importFromPath(vm, &fullPath, name);
        if(res != IMPORT_NOT_FOUND) {
            jsrBufferFree(&fullPath);
            return res == IMPORT_OK;
        }

        // Try to load a source package (__package__.jsr file in a directory)
        jsrBufferTrunc(&fullPath, moduleEnd);
        jsrBufferAppendStr(&fullPath, "/" PACKAGE_FILE JSR_EXT);

        res = importFromPath(vm, &fullPath, name);
        if(res != IMPORT_NOT_FOUND) {
            jsrBufferFree(&fullPath);
            return res == IMPORT_OK;
        }

        // If there is no package try to load compiled module (i.e. `.jsc` file)
        jsrBufferTrunc(&fullPath, moduleEnd);
        jsrBufferAppendStr(&fullPath, JSC_EXT);

        res = importFromPath(vm, &fullPath, name);
        if(res != IMPORT_NOT_FOUND) {
            jsrBufferFree(&fullPath);
            return res == IMPORT_OK;
        }

        // No binary module found, finally try with source module (i.e. `.jsr` file)
        jsrBufferTrunc(&fullPath, moduleEnd);
        jsrBufferAppendStr(&fullPath, JSR_EXT);

        res = importFromPath(vm, &fullPath, name);
        if(res != IMPORT_NOT_FOUND) {
            jsrBufferFree(&fullPath);
            return res == IMPORT_OK;
        }

        jsrBufferClear(&fullPath);
    }

    jsrBufferFree(&fullPath);
    return false;
}

bool importModule(JStarVM* vm, ObjString* name) {
    if(hashTableContainsKey(&vm->modules, name)) {
        push(vm, NULL_VAL);
        return true;
    }

    size_t len;
    const char* builtinBytecode = readBuiltInModule(name->data, &len);
    if(builtinBytecode != NULL) {
        JStarBuffer code = jsrBufferWrap(vm, builtinBytecode, len);
        return importBinary(vm, name->data, name, &code);
    }

    if(!importModuleOrPackage(vm, name)) {
        return false;
    }

    return true;
}

void parseErrorCallback(const char* file, int line, const char* error, void* udata) {
    JStarVM* vm = udata;
    reportError(vm, JSR_SYNTAX_ERR, file, line, error);
}
