#ifndef DISASSEMBLE_H
#define DISASSEMBLE_H

#include <stddef.h>

#include "chunk.h"

void disassembleChunk(Chunk *c);
void disassembleIstr(Chunk *c, size_t istr);

#endif
