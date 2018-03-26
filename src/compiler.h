#ifndef COMPILER_H
#define COMPILER_H

#include "memory.h"
#include "object.h"
#include "ast.h"

#include <stdint.h>

#define MAX_LOCALS UINT8_MAX

typedef struct VM VM;

typedef struct Local {
	Identifier id;
	int depth;
} Local;

typedef struct Compiler {
	VM *vm;
	bool topOfFile;
	struct Compiler *prev;
	uint8_t localsCount;
	Local locals[MAX_LOCALS];
	int depth;
	ObjFunction *func;
} Compiler;

void initCompiler(Compiler *c, Compiler *enclosing, VM *vm, bool topOfFile);
ObjFunction *compile(Compiler *c, Program *p);

void reachCompilerRoots(VM *vm, Compiler *c);

#endif
