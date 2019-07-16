#ifndef IMPORT_H
#define IMPORT_H

#include "object.h"
#include "vm.h"

#include "blparse/ast.h"

#define MAX_IMPORT_PATH_LEN 2048

ObjFunction *compileWithModule(BlangVM *vm, ObjString *name, Stmt *program);
void setModule(BlangVM *vm, ObjString *name, ObjModule *module);
ObjModule *getModule(BlangVM *vm, ObjString *name);
bool importModule(BlangVM *vm, ObjString *name);

#endif
