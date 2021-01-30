#ifndef DISASSEMBLE_H
#define DISASSEMBLE_H

#include <stddef.h>

#include "code.h"
#include "object.h"
#include "opcode.h"

void disassembleFunction(ObjFunction* fn);
void disassembleIstr(Code* c, size_t istr);
int opcodeArgsNumber(Opcode op);

#endif
