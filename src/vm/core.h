#ifndef CORE_H
#define CORE_H

#include "native.h"

typedef struct VM VM;

void initCoreLibrary(VM *vm);

NATIVE(bl_error);
NATIVE(bl_isInt);
NATIVE(bl_str);

#endif
