#ifndef PARSER_H
#define PARSER_H

#include "lex.h"
#include "ast.h"

#include <stdbool.h>

typedef struct Parser {
	Lexer lex;
	Token peek;
	TokenType prevType;
	bool panic;
	bool hadError;
} Parser;

Stmt *parse(Parser *p, const char *src);

#endif
