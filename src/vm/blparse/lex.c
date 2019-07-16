#include "lex.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *name;
    size_t length;
    TokenType type;
} Keyword;

// clang-format off

static Keyword keywords[] = {
    {"and",      3, TOK_AND},
    {"class",    5, TOK_CLASS},
    {"else",     4, TOK_ELSE},
    {"false",    5, TOK_FALSE},
    {"for",      3, TOK_FOR},
    {"fun",      3, TOK_FUN},
    {"native",   6, TOK_NAT},
    {"if",       2, TOK_IF},
    {"elif",     4, TOK_ELIF},
    {"null",     4, TOK_NULL},
    {"or",       2, TOK_OR},
    {"return",   6, TOK_RETURN},
    {"super",    5, TOK_SUPER},
    {"true",     4, TOK_TRUE},
    {"var",      3, TOK_VAR},
    {"while",    5, TOK_WHILE},
    {"import",   6, TOK_IMPORT},
    {"in",       2, TOK_IN},
    {"then",     4, TOK_THEN},
    {"do",       2, TOK_DO},
    {"begin",    5, TOK_BEGIN},
    {"end",      3, TOK_END},
    {"as",       2, TOK_AS},
    {"is",       2, TOK_IS},
    {"try",      3, TOK_TRY},
    {"ensure",   6, TOK_ENSURE},
    {"except",   6, TOK_EXCEPT},
    {"raise",    5, TOK_RAISE},
    {"continue", 8, TOK_CONTINUE},
    {"break",    5, TOK_BREAK},
    // sentinel
    {NULL,       0, TOK_EOF}
};

// clang-format on

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

static bool match(Lexer *lex, char c) {
    if(isAtEnd(lex)) return false;
    if(peekChar(lex) == c) {
        advance(lex);
        return true;
    }
    return false;
}

void initLexer(Lexer *lex, const char *src) {
    lex->source = src;
    lex->tokenStart = src;
    lex->current = src;
    lex->curr_line = 1;

    // skip shabang if present
    if(peekChar(lex) == '#' && peekChar2(lex) == '!') {
        while(!isAtEnd(lex)) {
            if(peekChar(lex) == '\n') break;
            advance(lex);
        }
    }
}

static void skipSpacesAndComments(Lexer *lex) {
    while(!isAtEnd(lex)) {
        switch(peekChar(lex)) {
        case '\\':
            if(peekChar2(lex) == '\n') {
                lex->curr_line++;
                advance(lex);
                advance(lex);
            } else {
                return;
            }
            break;
        case '\r':
        case '\t':
        case ' ':
            advance(lex);
            break;
        case '/':
            if(peekChar2(lex) == '/') {
                while(peekChar(lex) != '\n' && !isAtEnd(lex))
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

static bool isHex(char c) {
    return isNum(c) || (c >= 'a' && c <= 'f');
}

static bool isAlphaNum(char c) {
    return isAlpha(c) || isNum(c);
}

static void makeToken(Lexer *lex, Token *tok, TokenType type) {
    tok->type = type;
    tok->lexeme = lex->tokenStart;
    tok->length = (int)(lex->current - lex->tokenStart);
    tok->line = lex->curr_line;
}

static void eofToken(Lexer *lex, Token *tok) {
    tok->type = TOK_EOF;
    tok->lexeme = lex->current;
    tok->length = 0;
    tok->line = lex->curr_line;
}

static void integer(Lexer *lex) {
    while(isNum(peekChar(lex)))
        advance(lex);
}

static void number(Lexer *lex, Token *tok) {
    integer(lex);

    if(peekChar(lex) == '.' && isNum(peekChar2(lex))) {
        advance(lex);
        integer(lex);
    }

    if(match(lex, 'e')) {
        char c = peekChar(lex);
        if(c == '-' || c == '+') advance(lex);
        integer(lex);
    }

    makeToken(lex, tok, TOK_NUMBER);
}

static void hexNumber(Lexer *lex, Token *tok) {
    while(isHex(peekChar(lex)))
        advance(lex);

    if(match(lex, 'e')) {
        char c = peekChar(lex);
        if(c == '-' || c == '+') advance(lex);
        integer(lex);
    }

    makeToken(lex, tok, TOK_NUMBER);
}

static void string(Lexer *lex, char end, Token *tok) {
    while(peekChar(lex) != end && !isAtEnd(lex)) {
        if(peekChar(lex) == '\n') lex->curr_line++;
        if(peekChar(lex) == '\\' && peekChar2(lex) != '\0') advance(lex);
        advance(lex);
    }

    // unterminated string
    if(isAtEnd(lex)) {
        makeToken(lex, tok, TOK_UNTERMINATED_STR);
        return;
    }

    advance(lex);

    makeToken(lex, tok, TOK_STRING);
}

static void identifier(Lexer *lex, Token *tok) {
    while(isAlphaNum(peekChar(lex)))
        advance(lex);

    TokenType type = TOK_IDENTIFIER;

    // See if the identifier is a reserved word.
    size_t length = lex->current - lex->tokenStart;
    for(Keyword *keyword = keywords; keyword->name != NULL; keyword++) {
        if(length == keyword->length && memcmp(lex->tokenStart, keyword->name, length) == 0) {
            type = keyword->type;
            break;
        }
    }

    makeToken(lex, tok, type);
}

void nextToken(Lexer *lex, Token *tok) {
    skipSpacesAndComments(lex);

    if(isAtEnd(lex)) {
        eofToken(lex, tok);
        return;
    }

    lex->tokenStart = lex->current;
    char c = advance(lex);

    if(c == '0' && match(lex, 'x')) {
        hexNumber(lex, tok);
        return;
    }

    if(isNum(c) || (c == '.' && isNum(peekChar(lex)))) {
        number(lex, tok);
        return;
    }
    if(isAlpha(c)) {
        identifier(lex, tok);
        return;
    }

    switch(c) {
    case '(':
        makeToken(lex, tok, TOK_LPAREN);
        break;
    case ')':
        makeToken(lex, tok, TOK_RPAREN);
        break;
    case ';':
        makeToken(lex, tok, TOK_SEMICOLON);
        break;
    case ':':
        makeToken(lex, tok, TOK_COLON);
        break;
    case ',':
        makeToken(lex, tok, TOK_COMMA);
        break;
    case '[':
        makeToken(lex, tok, TOK_LSQUARE);
        break;
    case ']':
        makeToken(lex, tok, TOK_RSQUARE);
        break;
    case '^':
        makeToken(lex, tok, TOK_POW);
        break;
    case '\'':
    case '"':
        string(lex, c, tok);
        break;
    case '.':
        if(peekChar(lex) == '.' && peekChar2(lex) == '.') {
            advance(lex);
            advance(lex);
            makeToken(lex, tok, TOK_VARARG);
        } else {
            makeToken(lex, tok, TOK_DOT);
        }
        break;
    case '-':
        if(match(lex, '='))
            makeToken(lex, tok, TOK_MINUS_EQ);
        else
            makeToken(lex, tok, TOK_MINUS);
        break;
    case '+':
        if(match(lex, '='))
            makeToken(lex, tok, TOK_PLUS_EQ);
        else
            makeToken(lex, tok, TOK_PLUS);
        break;
    case '/':
        if(match(lex, '='))
            makeToken(lex, tok, TOK_DIV_EQ);
        else
            makeToken(lex, tok, TOK_DIV);
        break;
    case '*':
        if(match(lex, '='))
            makeToken(lex, tok, TOK_MULT_EQ);
        else
            makeToken(lex, tok, TOK_MULT);
        break;
    case '%':
        if(match(lex, '='))
            makeToken(lex, tok, TOK_MOD_EQ);
        else
            makeToken(lex, tok, TOK_MOD);
        break;
    case '!':
        if(match(lex, '='))
            makeToken(lex, tok, TOK_BANG_EQ);
        else
            makeToken(lex, tok, TOK_BANG);
        break;
    case '=':
        if(match(lex, '='))
            makeToken(lex, tok, TOK_EQUAL_EQUAL);
        else
            makeToken(lex, tok, TOK_EQUAL);
        break;
    case '<':
        if(match(lex, '='))
            makeToken(lex, tok, TOK_LE);
        else
            makeToken(lex, tok, TOK_LT);
        break;
    case '>':
        if(match(lex, '='))
            makeToken(lex, tok, TOK_GE);
        else
            makeToken(lex, tok, TOK_GT);
        break;
    case '#':
        if(match(lex, '#'))
            makeToken(lex, tok, TOK_HASH_HASH);
        else
            makeToken(lex, tok, TOK_HASH);
        break;
    case '\n':
        makeToken(lex, tok, TOK_NEWLINE);
        lex->curr_line++;
        break;
    default:
        makeToken(lex, tok, TOK_ERR);
        break;
    }
}

void rewindTo(Lexer *lex, Token *tok) {
    if(tok->lexeme == NULL) return;
    lex->tokenStart = lex->current = tok->lexeme;
    lex->curr_line = tok->line;
}
