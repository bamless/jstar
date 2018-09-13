#ifndef IMPORT_H
#define IMPORT_H

#include "object.h"
#include "vm.h"

#include "parse/ast.h"

#define MAX_IMPORT_PATH_LEN 2048

ObjFunction *compileWithModule(VM *vm, ObjString *name, Stmt *program);
void setModule(VM *vm, ObjString *name, ObjModule *module);
ObjModule *getModule(VM *vm, ObjString *name);
bool importModule(VM *vm, ObjString *name);

#endif
