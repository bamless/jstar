#ifndef PARSER_H
#define PARSER_H

#include "ast.h"
#include "blconf.h"
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

BLANG_API Stmt *parse(Parser *p, const char *fname, const char *src, bool silent);
BLANG_API Expr *parseExpression(Parser *p, const char *fname, const char *src, bool silent);

#endif
