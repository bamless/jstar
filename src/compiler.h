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
	struct Compiler *prev;
	uint8_t localsCount;
	Local locals[MAX_LOCALS];
	int depth;
	ObjFunction *func;
} Compiler;

void initCompiler(Compiler *c, Compiler *enclosing, int depth, VM *vm);
void endCompiler(Compiler *c);
ObjFunction *compile(Compiler *c, Stmt *s);

void reachCompilerRoots(VM *vm, Compiler *c);

#endif
