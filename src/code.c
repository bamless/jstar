#include "code.h"

#include <stdbool.h>
#include <stdint.h>

#include "array.h"
#include "conf.h"
#include "jstar.h"
#include "vm.h"  // IWYU pragma: export

void initCode(Code* c) {
    *c = (Code){0};
}

void freeCode(JStarVM* vm, Code* c) {
    arrayFree(vm, &c->bytecode);
    arrayFree(vm, &c->lines);
    arrayFree(vm, &c->consts);
    arrayFree(vm, &c->symbols);
}

size_t writeByte(JStarVM* vm, Code* c, uint8_t b, int line) {
    arrayAppend(vm, &c->bytecode, b);
    arrayAppend(vm, &c->lines, line);
    return c->bytecode.count - 1;
}

int getBytecodeSrcLine(const Code* c, size_t index) {
    if(!c->lines.items) return -1;
    JSR_ASSERT(index < c->lines.count, "Line buffer overflow");
    return c->lines.items[index];
}

int addConstant(JStarVM* vm, Code* c, Value constant) {
    if(c->consts.count == UINT16_MAX) return -1;

    for(size_t i = 0; i < c->consts.count; i++) {
        if(valueEquals(c->consts.items[i], constant)) {
            return i;
        }
    }

    arrayAppend(vm, &c->consts, constant);
    return c->consts.count - 1;
}

int addSymbol(JStarVM* vm, Code* c, uint16_t constant) {
    if(c->symbols.count == UINT16_MAX) return -1;
    arrayAppend(vm, &c->symbols, ((Symbol){.constant = constant}));
    return c->symbols.count - 1;
}
