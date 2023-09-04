#ifndef JSTAR_LEX_H
#define JSTAR_LEX_H

#include <stdlib.h>

#include "../conf.h"

JSTAR_API extern const char* JStarTokName[];

typedef enum JStarTokType {
#define TOKEN(tok, _) tok,
#include "token.def"
} JStarTokType;

typedef struct JStarTok {
    JStarTokType type;
    const char* lexeme;
    int length, line;
} JStarTok;

typedef struct JStarLex {
    const char* source;
    size_t sourceLen;
    const char* tokenStart;
    const char* current;
    int currLine;
} JStarLex;

JSTAR_API void jsrInitLexer(JStarLex* lex, const char* src, size_t len);
JSTAR_API void jsrNextToken(JStarLex* lex, JStarTok* tok);
JSTAR_API void jsrLexRewind(JStarLex* lex, JStarTok* tok);

#endif
