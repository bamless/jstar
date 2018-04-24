#ifndef COMPILER_H
#define COMPILER_H

#include "memory.h"
#include "object.h"
#include "ast.h"

#include <stdint.h>

#define MAX_LOCALS UINT8_MAX

typedef struct VM VM;

typedef struct Compiler Compiler;

ObjFunction *compile(VM *vm, Stmt *s);

void reachCompilerRoots(VM *vm, Compiler *c);

#endif
