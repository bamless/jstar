#ifndef JSTAR_PARSER_H
#define JSTAR_PARSER_H

#include <stddef.h>

#include "ast.h"
#include "jstar/conf.h"

typedef void (*ParseErrorCB)(const char* file, int line, const char* error, void* userData);

JSTAR_API JStarStmt* jsrParse(const char* path, const char* src, size_t len, ParseErrorCB errFn,
                              void* data);
JSTAR_API JStarExpr* jsrParseExpression(const char* path, const char* src, size_t len,
                                        ParseErrorCB errFn, void* data);

#endif
