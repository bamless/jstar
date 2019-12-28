#ifndef PARSER_H
#define PARSER_H

#include "ast.h"
#include "jstarconf.h"
#include "lex.h"

#include <stdbool.h>

// tokens that implicitly signal the end of a statement (without requiring a ';' or '\n')
#define IS_IMPLICIT_END(t) (                                                             \
    t == TOK_EOF || t == TOK_END || t == TOK_ELSE || t == TOK_ELIF || t == TOK_ENSURE || \
    t == TOK_EXCEPT                                                                      \
)

// Tokens that signal the end of a statement
#define IS_STATEMENT_END(t) (IS_IMPLICIT_END(t) || t == TOK_NEWLINE || t == TOK_SEMICOLON)

// Tokens that identify an lvalue, i.e. an assignable expression
#define IS_LVALUE(type) \
    (type == VAR_LIT || type == ACCESS_EXPR || type == ARR_ACC || type == TUPLE_LIT)

// Tokens that identify a constant expression
#define IS_CONSTANT_LITERAL(type) \
    (type == NUM_LIT || type == BOOL_LIT || type == STR_LIT || type == NULL_LIT)

// Tokens that signal the start of an expression
#define IS_EXPR_START(t) (                                                                       \
    t == TOK_NUMBER || t == TOK_TRUE || t == TOK_FALSE || t == TOK_IDENTIFIER ||                 \
    t == TOK_STRING ||t == TOK_NULL || t == TOK_SUPER  || t == TOK_LPAREN || t == TOK_LSQUARE || \
    t == TOK_BANG || t == TOK_MINUS || t == TOK_FUN || t == TOK_HASH || t == TOK_HASH_HASH ||    \
    t == TOK_LCURLY                                                                              \
)

JSTAR_API Stmt* parse(const char *fname, const char *src);
JSTAR_API Expr* parseExpression(const char *fname, const char *src);

#endif
