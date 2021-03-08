#include "code.h"

#include <stdbool.h>

#include "util.h"

#define CODE_DEF_SIZE  8
#define CODE_GROW_FACT 2

void initCode(Code* c) {
    *c = (Code){0};
    initValueArray(&c->consts);
}

void freeCode(Code* c) {
    free(c->bytecode);
    free(c->lines);
    freeValueArray(&c->consts);
}

static void growCode(Code* c) {
    c->capacity = c->capacity ? c->capacity * CODE_GROW_FACT : CODE_DEF_SIZE;
    c->bytecode = realloc(c->bytecode, c->capacity * sizeof(uint8_t));
}

static void growLines(Code* c) {
    c->lineCapacity = c->lineCapacity ? c->lineCapacity * CODE_GROW_FACT : CODE_DEF_SIZE;
    c->lines = realloc(c->lines, c->lineCapacity * sizeof(int));
}

static bool shouldGrow(const Code* c) {
    return c->size + 1 > c->capacity;
}

static void ensureCapacity(Code* c) {
    if(shouldGrow(c)) {
        growCode(c);
        growLines(c);
    }
}

size_t writeByte(Code* c, uint8_t b, int line) {
    ensureCapacity(c);
    c->bytecode[c->size] = b;
    c->lines[c->lineSize++] = line;
    return c->size++;
}

int getBytecodeSrcLine(Code* c, size_t index) {
    if(c->lines == NULL) return -1;
    ASSERT(index < c->lineSize, "Line buffer overflow");
    return c->lines[index];
}

int addConstant(Code* c, Value constant) {
    ValueArray* consts = &c->consts;
    if(consts->size == UINT16_MAX) return -1;

    for(int i = 0; i < consts->size; i++) {
        if(valueEquals(consts->arr[i], constant)) {
            return i;
        }
    }

    return valueArrayAppend(&c->consts, constant);
}
