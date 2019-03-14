#include "import.h"
#include "memory.h"
#include "compiler.h"
#include "hashtable.h"
#include "modules.h"

#include "parse/parser.h"
#include "util/stringbuf.h"

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

static Stmt *parseModule(const char *path, const char *source) {
	Parser p;
	Stmt *program = parse(&p, path, source);

	if(p.hadError) {
		freeStmt(program);
		return NULL;
	}

	return program;
}

static bool importWithSource(BlangVM *vm, const char* path, ObjString *name, const char *source) {
	Stmt *program = parseModule(path, source);
	if(program == NULL) {
		return false;
	}

	ObjFunction *module = compileWithModule(vm, name, program);
	freeStmt(program);

	if(module == NULL) {
		return false;
	}
	
	push(vm, OBJ_VAL(module));
	return true;
}

static bool importModuleOrPackage(BlangVM *vm, StringBuffer *importPath, ObjString *name, char **src) {
	// try to see if it is a package
	size_t packStart = importPath->len;
	sbuf_appendstr(importPath, "/__package__.bl");

	char *path = sbuf_get_backing_buf(importPath);
	if((*src = loadSource(path)) != NULL) {
		if(!importWithSource(vm, path, name, *src)) {
			free(src);
			return false;
		}
		return true;
	}

	// try to see if it's a normal module
	sbuf_truncate(importPath, packStart);
	sbuf_appendstr(importPath, ".bl");
	
	path = sbuf_get_backing_buf(importPath); 
	if((*src = loadSource(path)) != NULL) {
		if(!importWithSource(vm, path, name, *src)) {
			free(src);
			return false;
		}
		return true;
	}

	return false;
}

bool importModule(BlangVM *vm, ObjString *name) {
	if(hashTableContainsKey(&vm->modules, name)) {
		push(vm, NULL_VAL);
		return true;
	}

	// check if builtin
	const char *builtinSrc = NULL;
	if((builtinSrc = readBuiltInModule(name->data)) != NULL) {
		return importWithSource(vm, name->data, name, builtinSrc);
	}

	// try to read module or package
	StringBuffer sb;
	sbuf_create(&sb);

	char *src = NULL;

	ObjList *paths = vm->importpaths;
	for(size_t i = 0; i < paths->count; i++) {
		if(!IS_STRING(paths->arr[i])) {
			continue;
		}

		sbuf_appendstr(&sb, AS_STRING(paths->arr[i])->data);
		sbuf_appendchar(&sb, '/');

		size_t s = sb.len - 1;
		sbuf_appendstr(&sb, name->data);
		sbuf_replacechar(&sb, s, '.', '/');

		if(!importModuleOrPackage(vm, &sb, name, &src)) {
			sbuf_destroy(&sb);
			return false;
		}

		if(src != NULL) {
			break;
		}

		// not found, try the next path
		sbuf_clear(&sb);
	}


	// no module found
	if(src == NULL) {
		// try current cwd
		sbuf_appendstr(&sb, "./");

		size_t s = sb.len - 1;
		sbuf_appendstr(&sb, name->data);
		sbuf_replacechar(&sb, s, '.', '/');

		if(!importModuleOrPackage(vm, &sb, name, &src)) {
			sbuf_destroy(&sb); 
			return false;
		}

		if(src == NULL) {
			sbuf_destroy(&sb); 
			return false;
		}
	}

	sbuf_destroy(&sb); 
	free(src);

	// we loaded the module (or package), set simple name in parent package if any
	char *nameStart = strrchr(name->data, '.');

	// not a nested module, nothing to do
	if(nameStart == NULL) {
		return true;
	}
	nameStart++;


	ObjString *parentName = copyString(vm, name->data, nameStart - 1 - name->data);
	push(vm, OBJ_VAL(parentName));

	ObjString *simpleName = copyString(vm, nameStart, strlen(nameStart));

	ObjModule *module = getModule(vm, name);
	ObjModule *parent = getModule(vm, parentName);
	hashTablePut(&parent->globals, simpleName, OBJ_VAL(module));

	pop(vm);
	return true;
}
