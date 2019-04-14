#ifndef COMPILER_H
#define COMPILER_H

#include "memory.h"
#include "object.h"

#include "parse/ast.h"

#include <stdint.h>

#define CTOR_STR "new"

typedef struct BlangVM BlangVM;
typedef struct Compiler Compiler;

ObjFunction *compile(BlangVM *vm, ObjModule *module, Stmt *s);

void reachCompilerRoots(BlangVM *vm, Compiler *c);

#endif
