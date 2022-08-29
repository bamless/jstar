#ifndef COMPILER_H
#define COMPILER_H

#include <stdint.h>

#include "jstar.h"
#include "object.h"
#include "parse/ast.h"

#define CTOR_STR   "new"      // Special method name that signals a constructor
#define MAX_LOCALS UINT8_MAX  // At most 255 local variables per frame

typedef struct Compiler Compiler;

ObjFunction* compile(JStarVM* vm, const char* filename, ObjModule* module, JStarStmt* s);
void reachCompilerRoots(JStarVM* vm, Compiler* c);

#endif
