#ifndef LEX_H
#define LEX_H

#include "../jstarconf.h"

JSTAR_API extern const char* JStarTokName[];

typedef enum JStarTokType {
#define TOKEN(tok, _) tok,
#include "token.def"
} JStarTokType;

typedef struct JStarTok {
    JStarTokType type;
    const char* lexeme;
    int length;
    int line;
} JStarTok;

typedef struct JStarLex {
    const char* source;
    const char* tokenStart;
    const char* current;
    int currLine;
} JStarLex;

JSTAR_API void jsrInitLexer(JStarLex* lex, const char* src);
JSTAR_API void jsrNextToken(JStarLex* lex, JStarTok* tok);
JSTAR_API void jsrLexRewind(JStarLex* lex, JStarTok* tok);

#endif
