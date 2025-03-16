#include "code.h"

#include <stdbool.h>
#include <stdint.h>

#include "conf.h"
#include "util.h"

void initCode(Code* c) {
    *c = (Code){0};
    initValueArray(&c->consts);
}

void freeCode(Code* c) {
    free(c->bytecode);
    free(c->lines);
    freeValueArray(&c->consts);
    free(c->symbols);
}

size_t writeByte(Code* c, uint8_t b, int line) {
    ARRAY_APPEND(c, size, capacity, bytecode, b);
    ARRAY_APPEND(c, lineSize, lineCapacity, lines, line);
    return c->size - 1;
}

int getBytecodeSrcLine(const Code* c, size_t index) {
    if(c->lines == NULL) return -1;
    JSR_ASSERT(index < c->lineSize, "Line buffer overflow");
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

int addSymbol(Code* c, uint16_t constant) {
    if(c->symbolCount == UINT16_MAX) return -1;
    ARRAY_APPEND(c, symbolCount, symbolCapacity, symbols, (Symbol){.constant = constant});
    return c->symbolCount - 1;
}
