#ifndef LEX_H
#define LEX_H

#include "jstarconf.h"
#include "token.h"

typedef struct Lexer {
    const char* source;
    const char* tokenStart;
    const char* current;
    int curr_line;
} Lexer;

JSTAR_API void initLexer(Lexer* lex, const char* src);
JSTAR_API void nextToken(Lexer* lex, Token* tok);

JSTAR_API void rewindTo(Lexer* lex, Token* tok);

#endif
