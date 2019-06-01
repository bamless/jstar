#include "import.h"
#include "compiler.h"
#include "hashtable.h"
#include "memory.h"
#include "parser.h"

#include "builtin/modules.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#define PACKAGE_FILE "/__package__.bl"

static char *loadSource(const char *path) {
    FILE *srcFile = fopen(path, "rb+");
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

ObjFunction *compileWithModule(BlangVM *vm, ObjString *name, Stmt *program) {
    ObjModule *module = getModule(vm, name);

    if(module == NULL) {
        disableGC(vm, true);

        module = newModule(vm, name);
        hashTablePut(&module->globals, copyString(vm, "__name__", 8, true), OBJ_VAL(name));
        setModule(vm, name, module);

        disableGC(vm, false);
    }

    ObjFunction *fn = compile(vm, module, program);
    return fn;
}

void setModule(BlangVM *vm, ObjString *name, ObjModule *module) {
    hashTablePut(&vm->modules, name, OBJ_VAL(module));
}

ObjModule *getModule(BlangVM *vm, ObjString *name) {
    Value module;
    if(!hashTableGet(&vm->modules, name, &module)) {
        return NULL;
    }
    return AS_MODULE(module);
}

static bool importWithSource(BlangVM *vm, const char *path, ObjString *name, const char *source) {
    Parser p;
    Stmt *program = parse(&p, path, source, false);

    if(p.hadError) return false;

    ObjFunction *module = compileWithModule(vm, name, program);
    freeStmt(program);

    if(module == NULL) return false;

    push(vm, OBJ_VAL(module));
    return true;
}

static bool importFromPath(BlangVM *vm, const char *path, ObjString *name) {
    char *source = loadSource(path);
    if(source == NULL) return false;
    bool res = importWithSource(vm, path, name, source);
    free(source);
    return res;
}

static bool importModuleOrPackage(BlangVM *vm, ObjString *name) {
    ObjList *paths = vm->importpaths;

    BlBuffer fullPath;
    blBufferInit(vm, &fullPath);

    for(size_t i = 0; i < paths->count + 1; i++) {
        if(i == paths->count) {
            // We have run through all import paths, try CWD
            blBufferAppendstr(&fullPath, "./");
        } else {
            if(!IS_STRING(paths->arr[i])) continue;
            blBufferAppendstr(&fullPath, AS_STRING(paths->arr[i])->data);
            blBufferAppendChar(&fullPath, '/');
        }

        size_t moduleStart = fullPath.len - 1;
        blBufferAppendstr(&fullPath, name->data);
        blBufferReplaceChar(&fullPath, moduleStart, '.', '/');

        // try to load a package
        size_t moduleEnd = fullPath.len;
        blBufferAppendstr(&fullPath, PACKAGE_FILE);

        if(importFromPath(vm, fullPath.data, name)) {
            blBufferFree(&fullPath);
            return true;
        }

        // if there is no package try to load module (i.e. normal .bl file)
        blBufferTrunc(&fullPath, moduleEnd);
        blBufferAppendstr(&fullPath, ".bl");

        if(importFromPath(vm, fullPath.data, name)) {
            blBufferFree(&fullPath);
            return true;
        }
    }

    return false;
}

bool importModule(BlangVM *vm, ObjString *name) {
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
    if(nameStart == NULL) {
        return true;
    }
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
