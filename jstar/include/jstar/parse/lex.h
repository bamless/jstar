#ifndef LEX_H
#define LEX_H

#include "../jstarconf.h"

#define IS_COMPUND_ASSIGN(tok) (tok <= TOK_MOD_EQ && tok > TOK_EQUAL)
#define IS_ASSIGN(tok)         (tok <= TOK_MOD_EQ && tok >= TOK_EQUAL)

extern const char* TokenNames[];

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
    int currLine;
} Lexer;

JSTAR_API void jsrInitLexer(Lexer* lex, const char* src);
JSTAR_API void jsrNextToken(Lexer* lex, Token* tok);
JSTAR_API void jsrLexRewind(Lexer* lex, Token* tok);

#endif
