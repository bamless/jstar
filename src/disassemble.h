#ifndef DISASSEMBLE_H
#define DISASSEMBLE_H

#include <stddef.h>

#include "code.h"
#include "opcode.h"

void disassembleCode(Code* c);
void disassembleIstr(Code* c, size_t istr);
int opcodeArgsNumber(Opcode op);

#endif
