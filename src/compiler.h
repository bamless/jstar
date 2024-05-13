#ifndef COMPILER_H
#define COMPILER_H

#include <stdint.h>

#include "jstar.h"
#include "object.h"
#include "parse/ast.h"

#define MAX_LOCALS UINT8_MAX  // At most 255 local variables per frame

typedef struct Compiler Compiler;

ObjFunction* compile(JStarVM* vm, const char* filename, ObjModule* module, const JStarStmt* s);
void reachCompilerRoots(JStarVM* vm, Compiler* c);

#endif
