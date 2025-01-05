#ifndef DISASSEMBLE_H
#define DISASSEMBLE_H

#include <stddef.h>

#include "code.h"
#include "object_types.h"

void disassembleFunction(const ObjFunction* fn);
void disassembleNative(const ObjNative* nat);
void disassembleInstr(const Code* c, int indent, size_t istr);

#endif
