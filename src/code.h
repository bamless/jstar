#ifndef CHUNK_H
#define CHUNK_H

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "symbol.h"
#include "value.h"

typedef struct {
    uint8_t* items;
    size_t capacity, count;
} Bytecode;

typedef struct {
    int* items;
    size_t capacity, count;
} Lines;

typedef struct {
    Symbol* items;
    size_t capacity, count;
} Symbols;

// A runtime representation of a J* bytecode chunk.
// Stores the bytecode, the constants and the symbols used in the chunk, as well as metadata
// associated with each opcode (such as the original source line number).
typedef struct Code {
    Bytecode bytecode;
    Lines lines;
    Values consts;
    Symbols symbols;
} Code;

void initCode(Code* c);
void freeCode(Code* c);

size_t writeByte(Code* c, uint8_t b, int line);
int addConstant(Code* c, Value constant);
int addSymbol(Code* c, uint16_t constant);
int getBytecodeSrcLine(const Code* c, size_t index);

#endif
