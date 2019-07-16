#ifndef PARSER_H
#define PARSER_H

#include "ast.h"
#include "lex.h"

#include <stdbool.h>

typedef struct Parser {
    Lexer lex;
    Token peek;
    const char *fname;
    TokenType prevType;
    const char *lnStart;
    bool silent;
    bool panic;
    bool hadError;
} Parser;

Stmt *parse(Parser *p, const char *fname, const char *src, bool silent);
Expr *parseExpression(Parser *p, const char *fname, const char *src, bool silent);

#endif
