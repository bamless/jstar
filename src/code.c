#include "code.h"

#include <stdbool.h>
#include <stdint.h>

#include "array.h"
#include "conf.h"

void initCode(Code* c) {
    *c = (Code){0};
}

void freeCode(Code* c) {
    arrayFree(&c->bytecode);
    arrayFree(&c->lines);
    arrayFree(&c->consts);
    arrayFree(&c->symbols);
}

size_t writeByte(Code* c, uint8_t b, int line) {
    arrayAppend(&c->bytecode, b);
    arrayAppend(&c->lines, line);
    return c->bytecode.count - 1;
}

int getBytecodeSrcLine(const Code* c, size_t index) {
    if(!c->lines.items) return -1;
    JSR_ASSERT(index < c->lines.count, "Line buffer overflow");
    return c->lines.items[index];
}

int addConstant(Code* c, Value constant) {
    if(c->consts.count == UINT16_MAX) return -1;

    for(size_t i = 0; i < c->consts.count; i++) {
        if(valueEquals(c->consts.items[i], constant)) {
            return i;
        }
    }

    arrayAppend(&c->consts, constant);
    return c->consts.count - 1;
}

int addSymbol(Code* c, uint16_t constant) {
    if(c->symbols.count == UINT16_MAX) return -1;
    arrayAppend(&c->symbols, ((Symbol){.constant = constant}));
    return c->symbols.count - 1;
}
