#ifndef DISASSEMBLE_H
#define DISASSEMBLE_H

#include "chunk.h"

void disassemble(Chunk *c);
void disassembleIstr(Chunk *c, size_t istr);

#endif
