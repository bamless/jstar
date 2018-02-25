#ifndef LEX_H
#define LEX_H

#include "token.h"

#include <stdbool.h>

typedef struct Lexer {
	const char *source;
	const char *tokenStart;
	const char *current;
	int curr_line;
} Lexer;

void initLexer(Lexer *lex, const char *src);
void nextToken(Lexer *lex, Token *tok);

#endif
