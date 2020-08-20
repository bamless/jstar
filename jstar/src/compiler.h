#ifndef COMPILER_H
#define COMPILER_H

#include "jstar.h"
#include "object.h"
#include "parse/ast.h"

typedef struct Compiler Compiler;

ObjFunction* compile(JStarVM* vm, const char* filename, ObjModule* module, Stmt* s);
void reachCompilerRoots(JStarVM* vm, Compiler* c);

#endif
