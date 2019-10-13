#include "import.h"
#include "jstar.h"
#include "compiler.h"
#include "dynload.h"
#include "hashtable.h"
#include "memory.h"

#include "jsrparse/parser.h"

#include "builtin/modules.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#define PACKAGE_FILE "/__package__.jsr"

#ifdef __unix__
    #define DL_PREFIX "lib"
    #define DL_SUFFIX ".so"
#elif defined (__APPLE__) && defined (__MACH__)
    #define DL_PREFIX ""
    #define DL_SUFFIX ".dylib"
#elif defined(_WIN32)
    #define DL_PREFIX ""
    #define DL_SUFFIX ".dll"
#else
    #define DL_PREFIX ""
    #define DL_SUFFIX ""
#endif

static char *loadSource(const char *path) {
    FILE *srcFile = fopen(path, "rb");
    if(srcFile == NULL || errno == EISDIR) {
        if(srcFile) fclose(srcFile);
        return NULL;
    }

    fseek(srcFile, 0, SEEK_END);
    size_t size = ftell(srcFile);
    rewind(srcFile);

    char *src = malloc(size + 1);
    if(src == NULL) {
        fclose(srcFile);
        return NULL;
    }

    size_t read = fread(src, sizeof(char), size, srcFile);
    if(read < size) {
        free(src);
        fclose(srcFile);
        return NULL;
    }

    fclose(srcFile);

    src[read] = '\0';
    return src;
}

ObjFunction *compileWithModule(JStarVM *vm, ObjString *name, Stmt *program) {
    ObjModule *module = getModule(vm, name);

    if(module == NULL) {
        module = newModule(vm, name);
        hashTableImportNames(&module->globals, &vm->core->globals);
        setModule(vm, name, module);

        push(vm, OBJ_VAL(name));
        hashTablePut(&module->globals, copyString(vm, "__name__", 8, true), OBJ_VAL(name));
        pop(vm);
    }

    ObjFunction *fn = compile(vm, module, program);
    return fn;
}

void setModule(JStarVM *vm, ObjString *name, ObjModule *module) {
    push(vm, OBJ_VAL(module));
    push(vm, OBJ_VAL(name));
    hashTablePut(&module->globals, copyString(vm, "__name__", 8, true), OBJ_VAL(name));
    pop(vm);
    pop(vm);
    hashTablePut(&vm->modules, name, OBJ_VAL(module));
}

ObjModule *getModule(JStarVM *vm, ObjString *name) {
    Value module;
    if(!hashTableGet(&vm->modules, name, &module)) {
        return NULL;
    }
    return AS_MODULE(module);
}

static void loadNativeDynlib(JStarVM *vm, JStarBuffer *modulePath, ObjString *moduleName) {
    const char *rootPath = strrchr(modulePath->data, '/');
    const char *simpleName = strrchr(moduleName->data, '.');

    if(simpleName == NULL)
        simpleName = moduleName->data;
    else
        simpleName++;
 
    jsrBufferTrunc(modulePath, (int)(rootPath - modulePath->data));
    jsrBufferAppendstr(modulePath, "/");
    jsrBufferAppendstr(modulePath, DL_PREFIX);
    jsrBufferAppendstr(modulePath, simpleName);
    jsrBufferAppendstr(modulePath, DL_SUFFIX);

    void *dynlib = dynload(modulePath->data);
    if(dynlib != NULL) {
        jsrBufferClear(modulePath);
        jsrBufferAppendstr(modulePath, "jsr_open_");
        jsrBufferAppendstr(modulePath, simpleName);

        typedef JStarNativeReg* (*RegFunc)();
        RegFunc open_lib = (RegFunc) dynsim(dynlib, modulePath->data);
        
        if(open_lib == NULL) {
            dynfree(dynlib);
            return;
        }

        ObjModule *m = getModule(vm, moduleName);
        m->natives.dynlib = dynlib;
        m->natives.registry = (*open_lib)();
    }
}

static bool importWithSource(JStarVM *vm, const char *path, ObjString *name, const char *source) {
    Parser p;
    Stmt *program = parse(&p, path, source, false);

    if(p.hadError) return false;

    ObjFunction *moduleFun = compileWithModule(vm, name, program);
    freeStmt(program);

    if(moduleFun == NULL) return false;

    push(vm, OBJ_VAL(moduleFun));
    return true;
}

static bool importFromPath(JStarVM *vm, JStarBuffer *path, ObjString *name) {
    char *source = loadSource(path->data);
    if(source == NULL) return false;

    bool imported;
    if((imported = importWithSource(vm, path->data, name, source))) {
        loadNativeDynlib(vm, path, name);
    }
    free(source);
    
    return imported;
}

static bool importModuleOrPackage(JStarVM *vm, ObjString *name) {
    ObjList *paths = vm->importpaths;

    JStarBuffer fullPath;
    jsrBufferInit(vm, &fullPath);

    for(size_t i = 0; i < paths->count + 1; i++) {
        if(i == paths->count) {
            // We have run through all import paths, try CWD
            jsrBufferAppendstr(&fullPath, "./");
        } else {
            if(!IS_STRING(paths->arr[i])) continue;
            jsrBufferAppendstr(&fullPath, AS_STRING(paths->arr[i])->data);
            jsrBufferAppendChar(&fullPath, '/');
        }

        size_t moduleStart = fullPath.len - 1;
        jsrBufferAppendstr(&fullPath, name->data);
        jsrBufferReplaceChar(&fullPath, moduleStart, '.', '/');

        // try to load a package
        size_t moduleEnd = fullPath.len;
        jsrBufferAppendstr(&fullPath, PACKAGE_FILE);

        if(importFromPath(vm, &fullPath, name)) {
            jsrBufferFree(&fullPath);
            return true;
        }

        // if there is no package try to load module (i.e. normal .jsr file)
        jsrBufferTrunc(&fullPath, moduleEnd);
        jsrBufferAppendstr(&fullPath, ".jsr");

        if(importFromPath(vm, &fullPath, name)) {
            jsrBufferFree(&fullPath);
            return true;
        }

        jsrBufferClear(&fullPath);
    }

    return false;
}

bool importModule(JStarVM *vm, ObjString *name) {
    if(hashTableContainsKey(&vm->modules, name)) {
        push(vm, NULL_VAL);
        return true;
    }

    // check if builtin
    const char *builtinSrc = readBuiltInModule(name->data);
    if(builtinSrc != NULL) {
        return importWithSource(vm, name->data, name, builtinSrc);
    }

    if(!importModuleOrPackage(vm, name)) {
        return false;
    }

    // we loaded the module (or package), set simple name in parent package if any
    char *nameStart = strrchr(name->data, '.');

    // not a nested module, nothing to do
    if(nameStart == NULL) return true;

    nameStart++;
    ObjString *parentName = copyString(vm, name->data, nameStart - 1 - name->data, true);
    push(vm, OBJ_VAL(parentName));
    ObjString *simpleName = copyString(vm, nameStart, strlen(nameStart), true);
    ObjModule *module = getModule(vm, name);
    ObjModule *parent = getModule(vm, parentName);
    hashTablePut(&parent->globals, simpleName, OBJ_VAL(module));
    pop(vm);

    return true;
}
