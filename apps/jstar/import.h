#ifndef IMPORT_H
#define IMPORT_H

#include <jstar/jstar.h>

// Init the `importPaths` list by appending the script directory (or the current working
// directory if `scriptPath` is NULL) and all the paths present in the JSTARPATH env variable.
// All paths are converted to absolute ones.
bool initImports(JStarVM* vm, const char* scriptPath, bool ignoreEnv);
// Frees all resources associated with the import system
void freeImports(void);

// Callback called by the J* VM when it encounters an `import` statement
JStarImportResult importCallback(JStarVM* vm, const char* moduleName);

#endif
