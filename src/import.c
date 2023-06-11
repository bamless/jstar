#include "import.h"

#include <stdio.h>
#include <string.h>

#include "compiler.h"
#include "hashtable.h"
#include "jstar.h"
#include "lib/builtins.h"
#include "parse/parser.h"
#include "profiler.h"
#include "serialize.h"
#include "util.h"
#include "value.h"
#include "vm.h"

static ObjModule* getOrCreateModule(JStarVM* vm, const char* path, ObjString* name) {
    ObjModule* module = getModule(vm, name);
    if(module == NULL) {
        push(vm, OBJ_VAL(name));
        module = newModule(vm, path, name);
        setModule(vm, name, module);
        pop(vm);
    }
    return module;
}

ObjFunction* compileModule(JStarVM* vm, const char* path, ObjString* name, JStarStmt* program) {
    PROFILE_FUNC()
    ObjModule* module = getOrCreateModule(vm, path, name);
    ObjFunction* fn = compile(vm, path, module, program);
    return fn;
}

ObjFunction* deserializeModule(JStarVM* vm, const char* path, ObjString* name,
                               const JStarBuffer* code, JStarResult* err) {
    PROFILE_FUNC()
    ObjFunction* fn = deserialize(vm, getOrCreateModule(vm, path, name), code, err);
    if(*err == JSR_VERSION_ERR) {
        vm->errorCallback(vm, *err, path, -1, "Incompatible binary file version");
    }
    if(*err == JSR_DESERIALIZE_ERR) {
        vm->errorCallback(vm, *err, path, -1, "Malformed binary file");
    }
    return fn;
}

static void registerInParent(JStarVM* vm, ObjModule* module) {
    ObjString* name = module->name;
    const char* lastDot = strrchr(name->data, '.');

    // Not a submodule, nothing to do
    if(lastDot == NULL) {
        return;
    }

    const char* simpleName = lastDot + 1;
    ObjModule* parent = getModule(vm, copyString(vm, name->data, simpleName - name->data - 1));
    ASSERT(parent, "Submodule parent could not be found.");

    if(!module->registry) {
        module->registry = parent->registry;
    }

    hashTablePut(&parent->globals, copyString(vm, simpleName, strlen(simpleName)), OBJ_VAL(module));
}

void setModule(JStarVM* vm, ObjString* name, ObjModule* module) {
    hashTablePut(&vm->modules, name, OBJ_VAL(module));
    registerInParent(vm, module);
}

ObjModule* getModule(JStarVM* vm, ObjString* name) {
    Value module;
    if(!hashTableGet(&vm->modules, name, &module)) {
        return NULL;
    }
    return AS_MODULE(module);
}

static void parseError(const char* file, int line, const char* error, void* udata) {
    JStarVM* vm = udata;
    vm->errorCallback(vm, JSR_SYNTAX_ERR, file, line, error);
}

static ObjModule* importSource(JStarVM* vm, const char* path, ObjString* name, const char* src) {
    PROFILE_FUNC()

    JStarStmt* program = jsrParse(path, src, parseError, vm);
    if(program == NULL) {
        return NULL;
    }

    ObjFunction* fn = compileModule(vm, path, name, program);
    jsrStmtFree(program);

    if(fn == NULL) {
        return NULL;
    }

    push(vm, OBJ_VAL(fn));
    vm->sp[-1] = OBJ_VAL(newClosure(vm, fn));

    return fn->proto.module;
}

static ObjModule* importBinary(JStarVM* vm, const char* path, ObjString* name,
                               const JStarBuffer* code) {
    PROFILE_FUNC()

    JStarResult res;
    ObjFunction* fn = deserializeModule(vm, path, name, code, &res);
    if(res != JSR_SUCCESS) {
        return NULL;
    }

    push(vm, OBJ_VAL(fn));
    vm->sp[-1] = OBJ_VAL(newClosure(vm, fn));

    return fn->proto.module;
}

ObjModule* importModule(JStarVM* vm, ObjString* name) {
    PROFILE_FUNC()

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

    if(!vm->importCallback) {
        return NULL;
    }

    JStarImportResult res;

    {
        PROFILE("importModule::importCallback")
        res = vm->importCallback(vm, name->data);
    }

    if(!res.code) {
        return NULL;
    }

    JStarBuffer code = jsrBufferWrap(vm, res.code, res.codeLength);

    ObjModule* module;
    if(isCompiledCode(&code)) {
        module = importBinary(vm, res.path, name, &code);
    } else {
        module = importSource(vm, res.path, name, res.code);
    }

    if(res.finalize) {
        res.finalize(res.userData);
    }

    if(module == NULL) {
        return NULL;
    }

    if(res.reg) {
        module->registry = res.reg;
    }

    return module;
}
