#include "opcode.h"

// Create string names of opcodes
const char* OpcodeNames[] = {
#define OPCODE(opcode, args, stack) #opcode,
#include "opcode.def"
};

static const int argsNumber[] = {
#define OPCODE(opcode, args, stack) args,
#include "opcode.def"
};

static const int stackUsage[] = {
#define OPCODE(opcode, args, stack) stack,
#include "opcode.def"
};

int opcodeArgsNumber(Opcode op) {
    return argsNumber[op];
}

int opcodeStackUsage(Opcode op) {
    return stackUsage[op];
}
