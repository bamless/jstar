#include "import.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "compiler.h"
#include "const.h"
#include "dynload.h"
#include "hashtable.h"
#include "jstar.h"
#include "parse/parser.h"
#include "serialize.h"
#include "std/modules.h"
#include "value.h"
#include "vm.h"

static void setModuleInParent(JStarVM* vm, ObjModule* module) {
    ObjString* name = module->name;
    const char* lastDot = strrchr(name->data, '.');
    if(!lastDot) return;

    const char* simpleName = lastDot + 1;
    ObjModule* parent = getModule(vm, copyString(vm, name->data, simpleName - name->data - 1));
    ASSERT(parent, "Submodule parent could not be found.");

    hashTablePut(&parent->globals, copyString(vm, simpleName, strlen(simpleName)), OBJ_VAL(module));
}

ObjFunction* compileWithModule(JStarVM* vm, const char* file, ObjString* moduleName,
                               JStarStmt* program) {
    ObjModule* module = getModule(vm, moduleName);

    if(module == NULL) {
        push(vm, OBJ_VAL(moduleName));
        module = newModule(vm, moduleName);
        pop(vm);

        setModule(vm, moduleName, module);
        setModuleInParent(vm, module);
    }

    if(program != NULL) {
        ObjFunction* fn = compile(vm, file, module, program);
        return fn;
    }

    return NULL;
}

ObjFunction* deserializeWithModule(JStarVM* vm, ObjString* moduleName, const JStarBuffer* code) {
    ObjModule* module = getModule(vm, moduleName);

    if(module == NULL) {
        push(vm, OBJ_VAL(moduleName));
        module = newModule(vm, moduleName);
        pop(vm);

        setModule(vm, moduleName, module);
        setModuleInParent(vm, module);
    }

    ObjFunction* fn = deserialize(vm, module, code);
    return fn;
}

void setModule(JStarVM* vm, ObjString* name, ObjModule* module) {
    hashTablePut(&vm->modules, name, OBJ_VAL(module));
    hashTablePut(&module->globals, copyString(vm, "__name__", 8), OBJ_VAL(name));
}

ObjModule* getModule(JStarVM* vm, ObjString* name) {
    Value module;
    if(!hashTableGet(&vm->modules, name, &module)) {
        return NULL;
    }
    return AS_MODULE(module);
}

static void tryNativeLib(JStarVM* vm, JStarBuffer* modulePath, ObjString* moduleName) {
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

        typedef JStarNativeReg* (*RegFunc)(void);
        RegFunc getNativeReg = (RegFunc)dynsim(dynlib, modulePath->data);

        if(getNativeReg == NULL) {
            dynfree(dynlib);
            return;
        }

        ObjModule* m = getModule(vm, moduleName);
        m->natives.dynlib = dynlib;
        m->natives.registry = (*getNativeReg)();
    }
}

static bool importWithSource(JStarVM* vm, const char* path, ObjString* name, const char* source) {
    JStarStmt* program = jsrParse(path, source, vm->errorCallback);

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

    bool imported = importWithSource(vm, path->data, name, src.data);
    jsrBufferFree(&src);

    if(!imported) {
        return IMPORT_ERR;
    }

    tryNativeLib(vm, path, name);
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

        // try to load a package (__package__.jsr file in a directory)
        jsrBufferAppendStr(&fullPath, "/" PACKAGE_FILE);
        res = importFromPath(vm, &fullPath, name);

        if(res != IMPORT_NOT_FOUND) {
            jsrBufferFree(&fullPath);
            return res == IMPORT_OK;
        }

        // if there is no package try to load module (i.e. normal .jsr file)
        jsrBufferTrunc(&fullPath, moduleEnd);
        jsrBufferAppendStr(&fullPath, ".jsr");
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

    const char* builtinSrc = readBuiltInModule(name->data);
    if(builtinSrc != NULL) {
        return importWithSource(vm, name->data, name, builtinSrc);
    }

    if(!importModuleOrPackage(vm, name)) {
        return false;
    }

    return true;
}
