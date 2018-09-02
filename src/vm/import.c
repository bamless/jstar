#include "import.h"
#include "memory.h"
#include "compiler.h"
#include "hashtable.h"
#include "modules.h"

#include "parse/parser.h"

#include <stdio.h>
#include <string.h>
#include <errno.h>

static char *resolvePathFromModuleName(VM *vm, const char *mname) {
	char *importpath = vm->importpath == NULL ? "" : vm->importpath;
	char *fname = malloc(strlen(importpath) + strlen(mname) + 4); // +4 for NUL and `.bl`
	strcpy(fname, importpath);
	strcat(fname, mname);
	strcat(fname, ".bl");
	return fname;
}

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

ObjFunction *compileWithModule(VM *vm, ObjString *name, Stmt *program) {
	ObjModule *module = getModule(vm, name);

	if(module == NULL) {
		disableGC(vm, true);

		module = newModule(vm, name);

		ObjModule *core = getModule(vm, copyString(vm, "__core__", 8));
		if(core != NULL) {
			hashTableImportNames(&module->globals, &core->globals);
		}

		hashTablePut(&module->globals, copyString(vm, "__name__", 8), OBJ_VAL(name));

		setModule(vm, name, module);

		disableGC(vm, false);
	}

	ObjFunction *fn = compile(vm, module, program);

	return fn;
}

void setModule(VM *vm, ObjString *name, ObjModule *module) {
	hashTablePut(&vm->modules, name, OBJ_VAL(module));
}

ObjModule *getModule(VM *vm, ObjString *name) {
	Value module;
	if(!hashTableGet(&vm->modules, name, &module)) {
		return NULL;
	}
	return AS_MODULE(module);
}

bool importModule(VM *vm, ObjString *name) {
	if(hashTableContainsKey(&vm->modules, name)) {
		push(vm, NULL_VAL);
		return true;
	}

	char *fpath = NULL;
	const char *src = NULL;
	bool dyn = true;

	if((src = readBuiltInModule(name->data)) != NULL) {
		fpath = malloc(strlen("builtin/") + strlen(name->data) + 1);
		strcpy(fpath, "builtin/");
		strcat(fpath, name->data);
		dyn = false;
	} else {
		fpath = resolvePathFromModuleName(vm, name->data);
		src = loadSource(fpath);
	}

	if(src == NULL) {
		free(fpath);
		return false;
	}

	Parser p;
	Stmt *program = parse(&p, fpath, src);

	if(p.hadError) {
		freeStmt(program);
		free(fpath);
		if(dyn) free((char*)src);
		return false;
	}

	ObjFunction *fn = compileWithModule(vm, name, program);

	free(fpath);
	if(dyn) free((char*)src);
	freeStmt(program);

	if(fn == NULL) return false;

	push(vm, OBJ_VAL(fn));
	return true;
}
