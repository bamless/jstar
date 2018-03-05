#include "chunk.h"

#include <string.h>

void initChunk(Chunk *c) {
	c->size = CHUNK_DEFAULT_SIZE;
	c->linesSize = CHUNK_DEFAULT_SIZE;
	c->count = 0;
	c->linesCount = 0;
	c->code = malloc(sizeof(uint8_t) * CHUNK_DEFAULT_SIZE);
	c->lines = calloc(sizeof(int), CHUNK_DEFAULT_SIZE);
	initValueArray(&c->consts);
}

void freeChunk(Chunk *c) {
	c->size = 0;
	c->count = 0;
	c->linesSize = 0;
	c->linesCount = 0;
	free(c->code);
	free(c->lines);
	freeValueArray(&c->consts);
}

static void growCode(Chunk *c) {
	c->size *= CHUNK_GROW_FACT;
	c->code = realloc(c->code, c->size * sizeof(uint8_t));
}

static void growLines(Chunk *c) {
	size_t newSize = c->linesSize * CHUNK_GROW_FACT;
	c->lines = realloc(c->lines, newSize * sizeof(int));
	memset(c->lines + c->linesSize, 0, newSize - c->linesSize);
	c->linesSize = newSize;
}

static void runLengthAppend(Chunk *c, int line) {
	if(c->linesCount == 0 || c->lines[c->linesCount - 1] != line) {
		if(c->linesCount + 2 > c->linesSize)
			growLines(c);

		c->lines[c->linesCount]++;
		c->lines[c->linesCount + 1] = line;
		c->linesCount += 2;
	} else {
		c->lines[c->linesCount - 2]++;
	}
}

static int runLengthGet(Chunk *c, size_t index) {
	for(size_t i = 0, decoded = 0; i < c->linesCount; i += 2) {
		decoded += c->lines[i];
		if(index <= decoded - 1)
			return c->lines[i + 1];
	}
	return -1;
}

size_t writeByte(Chunk *c, uint8_t b, int line) {
	if(c->count + 1 > c->size)
		growCode(c);

	c->code[c->count] = b;
	runLengthAppend(c, line);
	return c->count++;
}

int getBytecodeSrcLine(Chunk *c, size_t index) {
	return runLengthGet(c, index);
}
