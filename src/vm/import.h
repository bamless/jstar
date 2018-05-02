#ifndef IMPORT_H
#define IMPORT_H

#include "object.h"
#include "ast.h"
#include "vm.h"

ObjFunction *compileWithModule(VM *vm, ObjString *name, Stmt *program);
void setModule(VM *vm, ObjString *name, ObjModule *module);
ObjModule *getModule(VM *vm, ObjString *name);
bool importModule(VM *vm, ObjString *name);


#endif
