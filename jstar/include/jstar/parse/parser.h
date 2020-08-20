#ifndef PARSER_H
#define PARSER_H

#include "../jstarconf.h"
#include "ast.h"

typedef void (*ParseErrorCB)(const char* file, int line, const char* error);

JSTAR_API Stmt* jsrParse(const char* path, const char* src, ParseErrorCB errorFun);
JSTAR_API Expr* jsrParseExpression(const char* path, const char* src, ParseErrorCB errorFun);

#endif
