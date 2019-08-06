#ifndef COMPILER_H
#define COMPILER_H

#include "object.h"

#include "jsrparse/ast.h"

#include <stdint.h>

#define CTOR_STR "new"

typedef struct JStarVM JStarVM;
typedef struct Compiler Compiler;

ObjFunction *compile(JStarVM *vm, ObjModule *module, Stmt *s);

void reachCompilerRoots(JStarVM *vm, Compiler *c);

#endif
