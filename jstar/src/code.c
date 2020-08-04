#include "code.h"

#define CODE_DEF_SIZE  8
#define CODE_GROW_FACT 2

void initCode(Code* c) {
    c->size = 0;
    c->linesSize = 0;
    c->count = 0;
    c->linesCount = 0;
    c->bytecode = NULL;
    c->lines = NULL;
    initValueArray(&c->consts);
}

void freeCode(Code* c) {
    c->size = 0;
    c->count = 0;
    c->linesSize = 0;
    c->linesCount = 0;
    free(c->bytecode);
    free(c->lines);
    freeValueArray(&c->consts);
}

static void growCode(Code* c) {
    c->size = c->size == 0 ? CODE_DEF_SIZE : c->size * CODE_GROW_FACT;
    c->bytecode = realloc(c->bytecode, c->size * sizeof(uint8_t));
}

static void growLines(Code* c) {
    c->linesSize = c->linesSize == 0 ? CODE_DEF_SIZE : c->linesSize * CODE_GROW_FACT;
    c->lines = realloc(c->lines, c->linesSize * sizeof(int));
}

size_t writeByte(Code* c, uint8_t b, int line) {
    if(c->count + 1 > c->size) {
        growCode(c);
        growLines(c);
    }
    c->bytecode[c->count] = b;
    c->lines[c->linesCount++] = line;
    return c->count++;
}

int getBytecodeSrcLine(Code* c, size_t index) {
    return c->lines[index];
}

int addConstant(Code* c, Value constant) {
    ValueArray* consts = &c->consts;
    if(consts->count == UINT16_MAX) return -1;

    for(int i = 0; i < consts->count; i++) {
        if(valueEquals(consts->arr[i], constant)) {
            return i;
        }
    }

    return valueArrayAppend(&c->consts, constant);
}
