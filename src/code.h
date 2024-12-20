#ifndef CHUNK_H
#define CHUNK_H

#include <stdint.h>
#include <stdlib.h>

#include "value.h"

typedef struct ObjClass ObjClass;

typedef struct Symbol {
    enum { SYMBOL_NONE, SYMBOL_METHOD, SYMBOL_FIELD, SIMBOL_GLOBAL } type;
    uint16_t constant;
    uintptr_t key;
    union {
        Value method;
        int offset;
    } as;
} Symbol;

typedef struct Code {
    size_t capacity, size;
    uint8_t* bytecode;
    size_t lineCapacity, lineSize;
    int* lines;
    ValueArray consts;
    int symbolCapacity, symbolCount;
    Symbol* symbols;
} Code;

void initCode(Code* c);
void freeCode(Code* c);

size_t writeByte(Code* c, uint8_t b, int line);
int addConstant(Code* c, Value constant);
int getBytecodeSrcLine(const Code* c, size_t index);

int addSymbol(Code* c, uint16_t constant);

#endif
