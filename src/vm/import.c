#include "import.h"
#include "memory.h"
#include "compiler.h"
#include "hashtable.h"
#include "modules.h"

#include "parse/parser.h"

#include <stdio.h>
#include <string.h>
#include <errno.h>

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

bool importModule(BlangVM *vm, ObjString *name) {
	if(hashTableContainsKey(&vm->modules, name)) {
		push(vm, NULL_VAL);
		return true;
	}

	char fpath[MAX_IMPORT_PATH_LEN];
	const char *src = NULL;
	bool dyn = true;

	if((src = readBuiltInModule(name->data)) != NULL) {
		int r = snprintf(fpath, MAX_IMPORT_PATH_LEN, "builtin %s", name->data);
		if(r >= MAX_IMPORT_PATH_LEN) {
			return false;
		}
		dyn = false;
	} else {
		ObjList *paths = vm->importpaths;
		for(size_t i = 0; i < paths->count; i++) {
			if(!IS_STRING(paths->arr[i])) {
				continue;
			}

			char *path = AS_STRING(paths->arr[i])->data;
			int r = snprintf(fpath, MAX_IMPORT_PATH_LEN, "%s/%s.bl", path, name->data);
			if(r >= MAX_IMPORT_PATH_LEN) {
				return false;
			}

			if((src = loadSource(fpath)) == NULL) {
				continue;
			}
		}

		// if the file is not found in the specified import paths try in CWD
		if(src == NULL) {
			int r = snprintf(fpath, MAX_IMPORT_PATH_LEN, "%s.bl", name->data);
			if(r >= MAX_IMPORT_PATH_LEN) {
				return false;
			}
			src = loadSource(fpath);
		}
	}

	if(src == NULL) {
		return false;
	}

	Parser p;
	Stmt *program = parse(&p, fpath, src);

	if(p.hadError) {
		freeStmt(program);
		if(dyn) free((char*)src);
		return false;
	}

	ObjFunction *fn = compileWithModule(vm, name, program);

	if(dyn) free((char*)src);
	freeStmt(program);

	if(fn == NULL) return false;

	push(vm, OBJ_VAL(fn));
	return true;
}
