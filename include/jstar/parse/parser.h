#ifndef JSTAR_PARSER_H
#define JSTAR_PARSER_H

#include "../conf.h"
#include "ast.h"

typedef void (*ParseErrorCB)(const char* file, int line, const char* error, void* userData);

JSTAR_API JStarStmt* jsrParse(const char* path, const char* src, ParseErrorCB errFn, void* data);
JSTAR_API JStarExpr* jsrParseExpression(const char* path, const char* src, ParseErrorCB errFn,
                                        void* data);

#endif
