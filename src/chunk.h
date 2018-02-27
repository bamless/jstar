#ifndef CHUNK_H
#define CHUNK_H

#include "value.h"

#include <stdlib.h>
#include <stdint.h>

typedef struct Chunk {
	size_t size;
	size_t count;
	uint8_t *code;
	ValueArray consts;
} Chunk;

void initChunk(Chunk *c);
void freeChunk(Chunk *c);
size_t writeByte(Chunk *c, uint8_t b);

#endif
