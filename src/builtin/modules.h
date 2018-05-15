#ifndef MODULES_H
#define MODULES_H

#include "object.h"

Native resolveBuiltIn(const char *module, const char *cls, const char *name);
const char *readBuiltInModule(const char *name);

#endif
