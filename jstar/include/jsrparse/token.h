#ifndef TOKEN_H
#define TOKEN_H

extern const char* TokenNames[];

#define IS_ASSIGN(tok)         (tok <= TOK_MOD_EQ && tok >= TOK_EQUAL)
#define IS_COMPUND_ASSIGN(tok) (tok <= TOK_MOD_EQ && tok > TOK_EQUAL)

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

#endif
