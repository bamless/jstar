#include "import.h"

#include <stdio.h>
#include <string.h>

#include "compiler.h"
#include "conf.h"
#include "hashtable.h"
#include "jstar.h"
#include "lib/builtins.h"
#include "object.h"
#include "parse/parser.h"
#include "profiler.h"
#include "serialize.h"
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

JStarResult deserializeModule(JStarVM* vm, const char* path, ObjString* name, const void* code,
                              size_t len, ObjFunction** out) {
    PROFILE_FUNC()
    JStarResult res = deserialize(vm, getOrCreateModule(vm, path, name), code, len, out);
    if(res == JSR_VERSION_ERR) {
        vm->errorCallback(vm, res, path, -1, "Incompatible binary file version");
    }
    if(res == JSR_DESERIALIZE_ERR) {
        vm->errorCallback(vm, res, path, -1, "Malformed binary file");
    }
    return res;
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
    JSR_ASSERT(parent, "Submodule parent could not be found.");

    if(!module->registry) {
        module->registry = parent->registry;
    }

    setGlobal(vm, parent, copyString(vm, simpleName, strlen(simpleName)), OBJ_VAL(module));
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

static ObjModule* importSource(JStarVM* vm, const char* path, ObjString* name, const char* src,
                               size_t len) {
    PROFILE_FUNC()

    JStarStmt* program = jsrParse(path, src, len, parseError, vm);
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

static ObjModule* importBinary(JStarVM* vm, const char* path, ObjString* name, const void* code,
                               size_t len) {
    PROFILE_FUNC()
    JSR_ASSERT(isCompiledCode(code, len), "`code` must be a valid compiled chunk");

    ObjFunction* fn;
    JStarResult res = deserializeModule(vm, path, name, code, len, &fn);
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
    const void* bltinCode = readBuiltInModule(name->data, &len);
    if(bltinCode != NULL) {
        return importBinary(vm, "builtin", name, bltinCode, len);
    }

    if(!vm->importCallback) {
        return NULL;
    }

    // An import callback is similar to a native function call (can use the J* API and can be
    // re-entrant), so setup the apiStack to the current stack pointer, so that push/pop operations
    // are relative to the current position
    size_t apiStackOffset = vm->apiStack - vm->stack;
    vm->apiStack = vm->sp;

    JStarImportResult res = vm->importCallback(vm, name->data);
    vm->apiStack = vm->stack + apiStackOffset;

    if(!res.code) {
        return NULL;
    }

    ObjModule* module;
    if(isCompiledCode(res.code, res.codeLength)) {
        module = importBinary(vm, res.path, name, res.code, res.codeLength);
    } else {
        module = importSource(vm, res.path, name, res.code, res.codeLength);
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
