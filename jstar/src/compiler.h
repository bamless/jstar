#ifndef COMPILER_H
#define COMPILER_H

#include "jsrparse/ast.h"
#include "object.h"

typedef struct JStarVM JStarVM;
typedef struct Compiler Compiler;

ObjFunction *compile(JStarVM *vm, ObjModule *module, Stmt *s);
void reachCompilerRoots(JStarVM *vm, Compiler *c);

#endif
