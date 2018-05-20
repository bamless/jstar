#ifndef OS_H
#define OS_H

#include "blang.h"

void sysInitArgs(int argc, const char **argv);

NATIVE(bl_platform);
NATIVE(bl_initArgs);

#endif
