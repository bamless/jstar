#include "lex.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
	const char *name;
	size_t length;
	TokenType type;
} Keyword;

static Keyword keywords[] = {
	{"and",     3, TOK_AND},
	{"class",   5, TOK_CLASS},
	{"else",    4, TOK_ELSE},
	{"false",   5, TOK_FALSE},
	{"for",     3, TOK_FOR},
	{"def",     3, TOK_DEF},
	{"if",      2, TOK_IF},
	{"null",     3, TOK_NULL},
	{"or",      2, TOK_OR},
	{"print",   5, TOK_PRINT},
	{"return",  6, TOK_RETURN},
	{"super" ,  5, TOK_SUPER},
	{"this",    4, TOK_THIS},
	{"true",    4, TOK_TRUE},
	{"var",     3, TOK_VAR},
	{"while",   5, TOK_WHILE},
	// Sentinel to mark the end of the array.
	{NULL,      0, TOK_EOF}
};

void initLexer(Lexer *lex, const char * src) {
	lex->source = src;
	lex->tokenStart = src;
	lex->current = src;
	lex->curr_line = 0;
}

static char advance(Lexer *lex) {
	lex->current++;
	return lex->current[-1];
}

static char peekChar(Lexer *lex) {
	return *lex->current;
}

static bool isAtEnd(Lexer *lex) {
	return peekChar(lex) == '\0';
}

static char peekChar2(Lexer *lex) {
	if(isAtEnd(lex)) return '\0';
	return lex->current[1];
}

static void skipSpacesAndComments(Lexer *lex) {
	for(;;) {
		switch(peekChar(lex)) {
		case '\r':
		case '\t':
		case ' ':
		 	advance(lex);
			break;
		case '\n':
			lex->curr_line++;
			advance(lex);
			break;
		case '/':
			if(peekChar2(lex) == '/') {
				while(peekChar(lex) != '\n')
					advance(lex);
			} else {
				return;
			}
			break;
		default:
			return;
		}
	}
}

static bool isAlpha(char c) {
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static bool isNum(char c) {
	return c >= '0' && c <= '9';
}

static bool isAlphaNum(char c) {
	return isAlpha(c) || isNum(c);
}

static void makeToken(Lexer *lex, Token *tok, TokenType type) {
	tok->type = type;
	tok->lexeme = lex->tokenStart;
	tok->length = (int) (lex->current - lex->tokenStart);
	tok->line = lex->curr_line;
}

static void errToken(Lexer *lex, Token *tok, const char *msg) {
	tok->type = TOK_ERR;
	tok->lexeme = msg;
	tok->length = strlen(msg);
	tok->line = lex->curr_line;
}

static bool match(Lexer *lex, char c) {
	if(isAtEnd(lex)) return false;
	if(peekChar(lex) == c) {
		advance(lex);
		return true;
	}
	return false;
}

static void number(Lexer *lex, Token *tok) {
	while(isNum(peekChar(lex)))
		advance(lex);

	if(match(lex, '.')) {
		while(isNum(peekChar(lex)))
			advance(lex);
	}

	makeToken(lex, tok, TOK_NUMBER);
}

static void string(Lexer *lex, Token *tok) {
	while(peekChar(lex) != '"' && !isAtEnd(lex)) {
		if(peekChar(lex) == '\n') lex->curr_line++;
		if(peekChar(lex) == '\\' && peekChar2(lex) != '\0')
			advance(lex);
		advance(lex);
	}

	//unterminated string
	if(isAtEnd(lex)) {
		errToken(lex, tok, "Unterminated string");
		return;
	}

	advance(lex);

	makeToken(lex, tok, TOK_STRING);
}

static void identifier(Lexer *lex, Token *tok) {
	while(isAlphaNum(peekChar(lex))) advance(lex);

	TokenType type = TOK_IDENTIFIER;

	// See if the identifier is a reserved word.
	size_t length = lex->current - lex->tokenStart;
	for(Keyword* keyword = keywords; keyword->name != NULL; keyword++) {
		if(length == keyword->length &&
				memcmp(lex->tokenStart, keyword->name, length) == 0) {
			type = keyword->type;
			break;
		}
	}

	makeToken(lex, tok, type);
}

void nextToken(Lexer *lex, Token *tok) {
	skipSpacesAndComments(lex);

	if(isAtEnd(lex)) {
		makeToken(lex, tok, TOK_EOF);
		return;
	}

	lex->tokenStart = lex->current;

	char c = advance(lex);

	if(isNum(c))   { number(lex, tok);     return; }
	if(isAlpha(c)) { identifier(lex, tok); return; }

	switch(c) {
	case '(': makeToken(lex, tok, TOK_LPAREN);    break;
	case ')': makeToken(lex, tok, TOK_RPAREN);    break;
	case '{': makeToken(lex, tok, TOK_LBRACE);    break;
	case '}': makeToken(lex, tok, TOK_RBRACE);    break;
	case ';': makeToken(lex, tok, TOK_SEMICOLON); break;
	case ',': makeToken(lex, tok, TOK_COMMA);     break;
	case '.': makeToken(lex, tok, TOK_DOT);       break;
	case '-': makeToken(lex, tok, TOK_MINUS);     break;
	case '+': makeToken(lex, tok, TOK_PLUS);      break;
	case '/': makeToken(lex, tok, TOK_DIV);       break;
	case '*': makeToken(lex, tok, TOK_MULT);      break;
	case '%': makeToken(lex, tok, TOK_MOD);       break;
	case '"': string(lex, tok);                   break;
	case '!':
		if(match(lex, '=')) makeToken(lex, tok, TOK_BANG_EQ);
		else makeToken(lex, tok, TOK_BANG);
		break;
	case '=':
		if(match(lex, '=')) makeToken(lex, tok, TOK_EQUAL_EQUAL);
		else makeToken(lex, tok, TOK_EQUAL);
		break;
	case '<':
		if(match(lex, '=')) makeToken(lex, tok, TOK_LE);
		else makeToken(lex, tok, TOK_LT);
		break;
	case '>':
		if(match(lex, '=')) makeToken(lex, tok, TOK_GE);
		else makeToken(lex, tok, TOK_GT);
		break;
	default:
		makeToken(lex, tok, TOK_ERR);
		break;
	}
}
