#ifndef DISASSEMBLE_H
#define DISASSEMBLE_H

#include <stddef.h>

#include "code.h"
#include "object.h"

void disassembleFunction(ObjFunction* fn);
void disassembleNative(ObjNative* nat);
void disassembleIstr(Code* c, int indent, size_t istr);

#endif
