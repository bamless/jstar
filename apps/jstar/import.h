#ifndef IMPORT_H
#define IMPORT_H

#include "jstar/jstar.h"

// Inits the`CLI` app import system
void initImports(JStarVM* vm, const char* scriptPath, bool ignoreEnv);
// Frees all resources associated with the import system
void freeImports(void);

// Callback called by the J* VM when it encounters an `import` statement
JStarImportResult importCallback(JStarVM* vm, const char* moduleName);

#endif