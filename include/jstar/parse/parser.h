#ifndef JSTAR_PARSER_H
#define JSTAR_PARSER_H

#include <stddef.h>

#include "ast.h"
#include "jstar/conf.h"
#include "lex.h"

typedef void (*ParseErrorCB)(const char* file, JStarLoc loc, const char* error, void* userData);

JSTAR_API JStarStmt* jsrParse(const char* path, const char* src, size_t len, ParseErrorCB errFn,
                              JStarASTArena* a, void* data);
JSTAR_API JStarExpr* jsrParseExpression(const char* path, const char* src, size_t len,
                                        ParseErrorCB errFn, JStarASTArena* a, void* data);

#endif
