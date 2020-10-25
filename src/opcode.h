#ifndef OPCODE_H
#define OPCODE_H

#include "util.h"

typedef enum Opcode {
#define OPCODE(opcode, _) opcode,
#include "opcode.def"
} Opcode;

#endif  // OPCODE_H
