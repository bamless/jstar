#include "chunk.h"

void initChunk(Chunk *c) {
    c->size = 0;
    c->linesSize = 0;
    c->count = 0;
    c->linesCount = 0;
    c->code = NULL;
    c->lines = NULL;
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
    c->size = c->size == 0 ? CHUNK_DEFAULT_SIZE : c->size * CHUNK_GROW_FACT;
    c->code = realloc(c->code, c->size * sizeof(uint8_t));
}

static void growLines(Chunk *c) {
    c->linesSize = c->linesSize == 0 ? CHUNK_DEFAULT_SIZE : c->linesSize * CHUNK_GROW_FACT;
    c->lines = realloc(c->lines, c->linesSize * sizeof(int));
}

size_t writeByte(Chunk *c, uint8_t b, int line) {
    if(c->count + 1 > c->size) {
        growCode(c);
        growLines(c);
    }

    c->code[c->count] = b;
    c->lines[c->linesCount++] = line;

    return c->count++;
}

int getBytecodeSrcLine(Chunk *c, size_t index) {
    return c->lines[index];
}

int addConstant(Chunk *c, Value constant) {
    ValueArray *consts = &c->consts;

    if(consts->count == UINT16_MAX) return -1;

    for(int i = 0; i < consts->count; i++) {
        if(valueEquals(consts->arr[i], constant)) {
            return i;
        }
    }

    return valueArrayAppend(&c->consts, constant);
}
