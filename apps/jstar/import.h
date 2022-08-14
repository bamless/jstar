#ifndef IMPORT_H
#define IMPORT_H

#include "jstar/jstar.h"

void initImports(JStarVM* vm, const char* scriptPath, bool ignoreEnv);
void freeImports(void);

JStarImportResult importCallback(JStarVM* vm, const char* moduleName);

#endif