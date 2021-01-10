#ifndef MODULES_H
#define MODULES_H

#include "jstar.h"

JStarNative resolveBuiltIn(const char* module, const char* cls, const char* name);
char* readBuiltInModule(const char* name, unsigned int *len);

#endif
