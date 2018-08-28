#ifndef COMPILER_H
#define COMPILER_H

#include "memory.h"
#include "object.h"

#include "parse/ast.h"

#include <stdint.h>

#define MAX_LOCALS UINT8_MAX
#define MAX_TRY_DEPTH 5

typedef struct VM VM;

typedef struct Compiler Compiler;

ObjFunction *compile(VM *vm, ObjModule *module, Stmt *s);

void reachCompilerRoots(VM *vm, Compiler *c);

#endif
