#ifndef CHUNK_H
#define CHUNK_H

#include <stdlib.h>
#include <stdint.h>

typedef struct Chunk {

} Chunk;

void initChunk(Chunk *c);
void freeChunk(Chunk *c);
size_t writeByte(Chunk *c, uint8_t b);

#endif
