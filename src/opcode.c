#include "opcode.h"

#include "conf.h"

// Create string names of opcodes
const char* OpcodeNames[] = {
#define OPCODE(opcode, args, stack) #opcode,
#include "opcode.def"
};

int opcodeArgsNumber(Opcode op) {
    // clang-format off
    switch(op) {
    #define OPCODE(opcode, args, stack) case opcode: return args;
    #include "opcode.def"
    }
    // clang-format on
    JSR_UNREACHABLE();
    return -1;
}
