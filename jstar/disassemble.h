#ifndef DISASSEMBLE_H
#define DISASSEMBLE_H

#include <stddef.h>

#include "code.h"

void disassembleCode(Code* c);
void disassembleIstr(Code* c, size_t istr);

#endif
