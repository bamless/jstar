#ifndef COMPILER_H
#define COMPILER_H

#include "ast.h"
#include "object.h"

#include <stdint.h>

#define CTOR_STR "new"

typedef struct BlangVM BlangVM;
typedef struct Compiler Compiler;

ObjFunction *compile(BlangVM *vm, ObjModule *module, Stmt *s);

void reachCompilerRoots(BlangVM *vm, Compiler *c);

#endif
