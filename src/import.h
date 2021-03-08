#ifndef IMPORT_H
#define IMPORT_H

#include "jstar.h"
#include "object.h"
#include "parse/ast.h"
#include "value.h"

ObjFunction* compileWithModule(JStarVM* vm, const char* path, ObjString* name, JStarStmt* program);
ObjFunction* deserializeWithModule(JStarVM* vm, const char* path, ObjString* name,
                                   const JStarBuffer* code, JStarResult* err);

void setModule(JStarVM* vm, ObjString* name, ObjModule* module);
ObjModule* getModule(JStarVM* vm, ObjString* name);
ObjModule* importModule(JStarVM* vm, ObjString* name);

#endif
