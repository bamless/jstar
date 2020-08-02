#ifndef IMPORT_H
#define IMPORT_H

#include <stdbool.h>

#include "jsrparse/ast.h"
#include "jstar.h"
#include "object.h"
#include "value.h"

ObjFunction* compileWithModule(JStarVM* vm, const char* fileName, ObjString* name, Stmt* program);
void setModule(JStarVM* vm, ObjString* name, ObjModule* module);
ObjModule* getModule(JStarVM* vm, ObjString* name);
bool importModule(JStarVM* vm, ObjString* name);

#endif
