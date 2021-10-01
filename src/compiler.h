#ifndef COMPILER_H
#define COMPILER_H

#include <stdint.h>

#include "jstar.h"
#include "object.h"
#include "parse/ast.h"

#define MAX_TRY_DEPTH   HANDLER_MAX  // Max depth of nested trys
#define MAX_LOCALS      UINT8_MAX    // At most 255 local vars per frame
#define MAX_INLINE_ARGS 10           // Max number of inline arguments for function call

#define CTOR_STR "new"  // Special method name that signals a constructor

typedef struct Compiler Compiler;

ObjFunction* compile(JStarVM* vm, const char* filename, ObjModule* module, JStarStmt* s);
void reachCompilerRoots(JStarVM* vm, Compiler* c);

#endif
