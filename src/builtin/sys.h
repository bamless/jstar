#ifndef OS_H
#define OS_H

#include "native.h"

void sysInitArgs(int argc, const char **argv);

NATIVE(bl_exit);
NATIVE(bl_getImportPaths);
NATIVE(bl_platform);
NATIVE(bl_gc);
NATIVE(bl_clock);
NATIVE(bl_gets);
NATIVE(bl_init);

#endif
