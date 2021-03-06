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
        setModule(vm, name, module);
        pop(vm);

        hashTablePut(&module->globals, copyString(vm, "__name__", 8), OBJ_VAL(name));
        hashTableMerge(&module->globals, &vm->core->globals);  // implicitly import core
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
    ObjFunction* fn = deserialize(vm, getOrCreateModule(vm, name), code, err);
    if(*err == JSR_VERSION_ERR) {
        vm->errorCallback(vm, *err, path, -1, "Incompatible binary file version");
    }
    if(*err == JSR_DESERIALIZE_ERR) {
        vm->errorCallback(vm, *err, path, -1, "Malformed binary file");
    }
    return fn;
}

static void registerInParent(JStarVM* vm, ObjModule* mod) {
    ObjString* name = mod->name;
    const char* lastDot = strrchr(name->data, '.');
    if(lastDot == NULL) return;  // Not a submodule, nothing to do

    const char* simpleName = lastDot + 1;
    ObjModule* parent = getModule(vm, copyString(vm, name->data, simpleName - name->data - 1));
    ASSERT(parent, "Submodule parent could not be found.");
    
    hashTablePut(&parent->globals, copyString(vm, simpleName, strlen(simpleName)), OBJ_VAL(mod));
}

void setModule(JStarVM* vm, ObjString* name, ObjModule* mod) {
    hashTablePut(&vm->modules, name, OBJ_VAL(mod));
    registerInParent(vm, mod);
}

ObjModule* getModule(JStarVM* vm, ObjString* name) {
    Value module;
    if(!hashTableGet(&vm->modules, name, &module)) {
        return NULL;
    }
    return AS_MODULE(module);
}

static void loadNativeExtension(JStarVM* vm, JStarBuffer* modulePath, ObjString* moduleName) {
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

static void parseError(const char* file, int line, const char* error, void* udata) {
    JStarVM* vm = udata;
    vm->errorCallback(vm, JSR_SYNTAX_ERR, file, line, error);
}

static ObjModule* importSource(JStarVM* vm, const char* path, ObjString* name, const char* src) {
    JStarStmt* program = jsrParse(path, src, parseError, vm);
    if(program == NULL) {
        return NULL;
    }

    ObjFunction* fn = compileWithModule(vm, path, name, program);
    jsrStmtFree(program);

    if(fn == NULL) {
        return NULL;
    }

    push(vm, OBJ_VAL(fn));
    vm->sp[-1] = OBJ_VAL(newClosure(vm, fn));

    return fn->c.module;
}

static ObjModule* importBinary(JStarVM* vm, const char* path, ObjString* name,
                               const JStarBuffer* code) {
    JStarResult res;
    ObjFunction* fn = deserializeWithModule(vm, path, name, code, &res);
    if(res != JSR_SUCCESS) {
        return NULL;
    }

    push(vm, OBJ_VAL(fn));
    vm->sp[-1] = OBJ_VAL(newClosure(vm, fn));

    return fn->c.module;
}

typedef enum ImportRes {
    IMPORT_OK,
    IMPORT_ERR,
    IMPORT_NOT_FOUND,
} ImportRes;

static ImportRes importFromPath(JStarVM* vm, JStarBuffer* path, ObjString* name, ObjModule** res) {
    JStarBuffer src;
    if(!jsrReadFile(vm, path->data, &src)) {
        return IMPORT_NOT_FOUND;
    }

    if(isCompiledCode(&src)) {
        *res = importBinary(vm, path->data, name, &src);
    } else {
        *res = importSource(vm, path->data, name, src.data);
    }

    jsrBufferFree(&src);

    if(*res == NULL) {
        return IMPORT_ERR;
    }

    loadNativeExtension(vm, path, name);
    return IMPORT_OK;
}

static ObjModule* importModuleOrPackage(JStarVM* vm, ObjString* name) {
    ObjList* paths = vm->importPaths;

    JStarBuffer fullPath;
    jsrBufferInit(vm, &fullPath);

    for(size_t i = 0; i < paths->size + 1; i++) {
        if(i < paths->size) {
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

        ImportRes res;
        ObjModule* mod;

        // Try to load a binary package (__package__.jsc file in a directory)
        jsrBufferAppendStr(&fullPath, "/" PACKAGE_FILE JSC_EXT);
        res = importFromPath(vm, &fullPath, name, &mod);

        if(res != IMPORT_NOT_FOUND) {
            jsrBufferFree(&fullPath);
            return res == IMPORT_OK ? mod : NULL;
        }

        // Try to load a source package (__package__.jsr file in a directory)
        jsrBufferTrunc(&fullPath, moduleEnd);
        jsrBufferAppendStr(&fullPath, "/" PACKAGE_FILE JSR_EXT);
        res = importFromPath(vm, &fullPath, name, &mod);

        if(res != IMPORT_NOT_FOUND) {
            jsrBufferFree(&fullPath);
            return res == IMPORT_OK ? mod : NULL;
        }

        // If there is no package try to load compiled module (i.e. `.jsc` file)
        jsrBufferTrunc(&fullPath, moduleEnd);
        jsrBufferAppendStr(&fullPath, JSC_EXT);
        res = importFromPath(vm, &fullPath, name, &mod);

        if(res != IMPORT_NOT_FOUND) {
            jsrBufferFree(&fullPath);
            return res == IMPORT_OK ? mod : NULL;
        }

        // No binary module found, finally try with source module (i.e. `.jsr` file)
        jsrBufferTrunc(&fullPath, moduleEnd);
        jsrBufferAppendStr(&fullPath, JSR_EXT);
        res = importFromPath(vm, &fullPath, name, &mod);

        if(res != IMPORT_NOT_FOUND) {
            jsrBufferFree(&fullPath);
            return res == IMPORT_OK ? mod : NULL;
        }

        jsrBufferClear(&fullPath);
    }

    jsrBufferFree(&fullPath);
    return NULL;
}

ObjModule* importModule(JStarVM* vm, ObjString* name) {
    if(hashTableContainsKey(&vm->modules, name)) {
        push(vm, NULL_VAL);
        return getModule(vm, name);
    }

    size_t len;
    const char* builtinBytecode = readBuiltInModule(name->data, &len);
    if(builtinBytecode != NULL) {
        JStarBuffer code = jsrBufferWrap(vm, builtinBytecode, len);
        return importBinary(vm, name->data, name, &code);
    }

    return importModuleOrPackage(vm, name);
}