#ifndef OPCODE_H
#define OPCODE_H

extern const char* OpcodeNames[];

// Enum encoding the opcodes of the J* vm.
// The opcodes are generated from the "opcode.def" file.
typedef enum Opcode {
#define OPCODE(opcode, args, stack) opcode,
#include "opcode.def"
} Opcode;

// Returns the number of arguments of an opcode.
int opcodeArgsNumber(Opcode op);

// Returns the stack usage of an opcode (positive added to the stack, negative removed)
int opcodeStackUsage(Opcode op);

#endif
