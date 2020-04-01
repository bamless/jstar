#ifndef CHUNK_H
#define CHUNK_H

#include "value.h"

#include <stdint.h>
#include <stdlib.h>

#define CHUNK_DEFAULT_SIZE  8
#define CHUNK_GROW_FACT     2

typedef struct Chunk {
    size_t size, count;
    uint8_t *code;
    size_t linesSize, linesCount;
    int *lines;
    ValueArray consts;
} Chunk;

void initChunk(Chunk *c);
void freeChunk(Chunk *c);
size_t writeByte(Chunk *c, uint8_t b, int line);
int addConstant(Chunk *c, Value constant);
int getBytecodeSrcLine(Chunk *c, size_t index);

#endif
