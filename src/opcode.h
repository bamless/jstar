#ifndef OPCODE_H
#define OPCODE_H

extern const char* OpcodeNames[];

typedef enum Opcode {
#define OPCODE(opcode, args, stack) opcode,
#include "opcode.def"
} Opcode;

int opcodeArgsNumber(Opcode op);

#endif
