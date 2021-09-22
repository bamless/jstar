#ifndef IMPORT_H
#define IMPORT_H

#include "jstar.h"
#include "object.h"
#include "parse/ast.h"
#include "value.h"

ObjFunction* compileWithModule(JStarVM* vm, const char* path, ObjString* mod, JStarStmt* program);
ObjFunction* deserializeWithModule(JStarVM* vm, const char* path, ObjString* mod,
                                   const JStarBuffer* code, JStarResult* err);

void setModule(JStarVM* vm, ObjString* name, ObjModule* moduleName);
ObjModule* getModule(JStarVM* vm, ObjString* moduleName);
ObjModule* importModule(JStarVM* vm, ObjString* moduleName);

#endif
