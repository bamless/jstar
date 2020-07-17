#ifndef LEX_H
#define LEX_H

#include "jstarconf.h"

extern const char* TokenNames[];

#define IS_COMPUND_ASSIGN(tok) (tok <= TOK_MOD_EQ && tok > TOK_EQUAL)
#define IS_ASSIGN(tok)         (tok <= TOK_MOD_EQ && tok >= TOK_EQUAL)

typedef enum TokenType {
#define TOKEN(tok, _) tok,
#include "token.def"
} TokenType;

typedef struct Token {
    TokenType type;
    const char* lexeme;
    int length;
    int line;
} Token;

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
