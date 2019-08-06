#ifndef PARSER_H
#define PARSER_H

#include "ast.h"
#include "jstarconf.h"
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

JSTAR_API Stmt *parse(Parser *p, const char *fname, const char *src, bool silent);
JSTAR_API Expr *parseExpression(Parser *p, const char *fname, const char *src, bool silent);

#endif
