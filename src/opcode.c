#include "opcode.h"

#include "util.h"

// Create string names of opcodes
const char* OpcodeNames[] = {
#define OPCODE(opcode, _) #opcode,
#include "opcode.def"
};

int opcodeArgsNumber(Opcode op) {
    // clang-format off
    switch(op) {
    #define OPCODE(opcode, args) case opcode: return args;
    #include "opcode.def"
    }
    // clang-format on
    UNREACHABLE();
    return -1;
}