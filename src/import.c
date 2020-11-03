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
#include "std/modules.h"
#include "value.h"
#include "vm.h"

ObjFunction* compileWithModule(JStarVM* vm, const char* fileName, ObjString* name,
                               JStarStmt* program) {
    ObjModule* module = getModule(vm, name);

    if(module == NULL) {
        push(vm, OBJ_VAL(name));
        module = newModule(vm, name);
        pop(vm);

        setModule(vm, name, module);
    }

    if(program != NULL) {
        ObjFunction* fn = compile(vm, fileName, module, program);
        return fn;
    }

    return NULL;
}

static void setModuleInParent(JStarVM* vm, ObjModule* module) {
    ObjString* name = module->name;
    const char* lastDot = strrchr(name->data, '.');
    if(lastDot) {
        const char* simpleNameStart = lastDot + 1;

        ObjString* parentName = copyString(vm, name->data, simpleNameStart - name->data - 1);
        ObjModule* parent = getModule(vm, parentName);
        ASSERT(parent, "Submodule parent could not be found.");

        ObjString* simpleName = copyString(vm, simpleNameStart, strlen(simpleNameStart));
        hashTablePut(&parent->globals, simpleName, OBJ_VAL(module));
    }
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
    char* source = jsrReadFile(path->data);
    if(source == NULL) {
        return IMPORT_NOT_FOUND;
    }

    bool imported = importWithSource(vm, path->data, name, source);
    free(source);

    if(!imported) {
        return IMPORT_ERR;
    }

    tryNativeLib(vm, path, name);
    return IMPORT_OK;
}

static bool importModuleOrPackage(JStarVM* vm, ObjString* name) {
    ObjList* paths = vm->importpaths;

    JStarBuffer fullPath;
    jsrBufferInit(vm, &fullPath);

    for(size_t i = 0; i < paths->count + 1; i++) {
        if(i < paths->count) {
            if(!IS_STRING(paths->arr[i])) continue;
            jsrBufferAppendstr(&fullPath, AS_STRING(paths->arr[i])->data);
            if(fullPath.len > 0 && fullPath.data[fullPath.len - 1] != '/') {
                jsrBufferAppendChar(&fullPath, '/');
            }
        }

        size_t moduleStart = fullPath.len;
        size_t moduleEnd = moduleStart + name->length;
        jsrBufferAppendstr(&fullPath, name->data);
        jsrBufferReplaceChar(&fullPath, moduleStart, '.', '/');

        ImportResult res;

        // try to load a package (__package__.jsr file in a directory)
        jsrBufferAppendstr(&fullPath, "/" PACKAGE_FILE);
        res = importFromPath(vm, &fullPath, name);

        if(res != IMPORT_NOT_FOUND) {
            jsrBufferFree(&fullPath);
            return res == IMPORT_OK;
        }

        // if there is no package try to load module (i.e. normal .jsr file)
        jsrBufferTrunc(&fullPath, moduleEnd);
        jsrBufferAppendstr(&fullPath, ".jsr");
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
