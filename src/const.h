#ifndef CONST_H
#define CONST_H

#include <stdint.h>

#include "compiler.h"

// VM runtime constants
#define MAX_FRAMES    100000                         // Max number of frames (J* recursion depth)
#define MAX_REENTRANT 1000                           // Max allowed recursion for reentrant calls
#define FRAME_SZ      100                            // Default starting number of frames
#define HANDLER_MAX   6                              // Max number of try-excepts for a frame
#define STACK_SZ      (FRAME_SZ) * (MAX_LOCALS + 1)  // Deafult starting stack size
#define SUPER_SLOT    0                              // Constant holding the method's super-class

// GC constants
#define FIRST_GC       (1024 * 1024 * 20)  // 20MiB - First GC collection point
#define HEAP_GROW_RATE 2                   // How much the heap will grow after a gc

#endif
