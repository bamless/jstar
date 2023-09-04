#ifndef BUILTINS_H
#define BUILTINS_H

#include <stddef.h>

#include "jstar.h"

JStarNative resolveBuiltIn(const char* module, const char* cls, const char* name);
const void* readBuiltInModule(const char* name, size_t* len);

#endif
