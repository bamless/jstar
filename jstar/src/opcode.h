#ifndef OPCODE_H
#define OPCODE_H

typedef enum Opcode {
#define OPCODE(opcode, _) opcode,
#include "opcode.def"
} Opcode;

static inline int opcodeArgsNumber(Opcode op) {
    // clang-format off
    switch(op) {
    #define OPCODE(opcode, args) case opcode: return args;
    #include "opcode.def"
    }
    // clang-format on
    UNREACHABLE();
    return -1;
}

#endif  // OPCODE_H
