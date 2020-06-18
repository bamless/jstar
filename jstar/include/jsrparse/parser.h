#ifndef PARSER_H
#define PARSER_H

#include "ast.h"
#include "jstarconf.h"

typedef void (*ParseErrorFun)(const char* file, int line, const char* error);

JSTAR_API Stmt* parse(const char* path, const char* src, ParseErrorFun errorFun);
JSTAR_API Expr* parseExpression(const char* path, const char* src, ParseErrorFun errorFun);

#endif
