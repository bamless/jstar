#ifndef DISASSEMBLE_H
#define DISASSEMBLE_H

#include "chunk.h"

#ifdef DBG_PRINT_EXEC
void disassembleIstr(Chunk *c, size_t istr);
#endif

#endif
