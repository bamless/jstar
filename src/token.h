#ifndef H
#define H

extern const char *tokNames[];

typedef enum {
	TOK_LPAREN,
	TOK_RPAREN,
	TOK_LBRACE,
	TOK_RBRACE,
	TOK_BANG,
	TOK_BANG_EQ,
	TOK_COMMA,
	TOK_DOT,
	TOK_EQUAL,
	TOK_EQUAL_EQUAL,
	TOK_GT,
	TOK_GE,
	TOK_LT,
	TOK_LE,
	TOK_MINUS,
	TOK_PLUS,
	TOK_SEMICOLON,
	TOK_DIV,
	TOK_MULT,
	TOK_MOD,

	TOK_IDENTIFIER,
	TOK_STRING,
	TOK_NUMBER,

	TOK_AND,
	TOK_CLASS,
	TOK_ELSE,
	TOK_FALSE,
	TOK_DEF,
	TOK_FOR,
	TOK_IF,
	TOK_NULL,
	TOK_OR,
	TOK_PRINT,
	TOK_RETURN,
	TOK_SUPER,
	TOK_TRUE,
	TOK_VAR,
	TOK_WHILE,

	TOK_ERR,
	TOK_EOF
} TokenType;

typedef struct Token {
	TokenType type;
	const char *lexeme;
	int length;
	int line;
} Token;

#endif
