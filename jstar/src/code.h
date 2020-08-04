#ifndef CHUNK_H
#define CHUNK_H

#include <stdint.h>
#include <stdlib.h>

#include "value.h"

typedef struct Code {
    size_t size, count;
    uint8_t* bytecode;
    size_t linesSize, linesCount;
    int* lines;
    ValueArray consts;
} Code;

void initCode(Code* c);
void freeCode(Code* c);
size_t writeByte(Code* c, uint8_t b, int line);
int addConstant(Code* c, Value constant);
int getBytecodeSrcLine(Code* c, size_t index);

#endif
