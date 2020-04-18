#ifndef PARSER_H
#define PARSER_H

#include "ast.h"
#include "jstarconf.h"

JSTAR_API Stmt* parse(const char* fname, const char* src);
JSTAR_API Expr* parseExpression(const char* fname, const char* src);

#endif
