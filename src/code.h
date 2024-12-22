#ifndef CHUNK_H
#define CHUNK_H

#include <stdint.h>
#include <stdlib.h>

#include "value.h"

// A symbol poointing to a constant in the constant pool.
// Includes a cache for the value of the resolution.
// It can either be a method, field or global variable.
// Used to implement a caching scheme during VM evaluation.
typedef struct Symbol {
    enum { SYMBOL_METHOD, SYMBOL_FIELD, SIMBOL_GLOBAL } type;
    uint16_t constant;
    Obj* key;
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
int addSymbol(Code* c, uint16_t constant);
int getBytecodeSrcLine(const Code* c, size_t index);

#endif
