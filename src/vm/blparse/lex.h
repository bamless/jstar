#ifndef LEX_H
#define LEX_H

#include "blconf.h"
#include "token.h"

typedef struct Lexer {
    const char *source;
    const char *tokenStart;
    const char *current;
    int curr_line;
} Lexer;

BLANG_API void initLexer(Lexer *lex, const char *src);
BLANG_API void nextToken(Lexer *lex, Token *tok);

BLANG_API void rewindTo(Lexer *lex, Token *tok);

#endif
