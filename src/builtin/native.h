#ifndef NATIVE_H
#define NATIVE_H

#include <stdint.h>

#include "memory.h"

#define NATIVE(name) Value name(VM *vm, uint8_t argc, Value *args)

#endif
