#include "parse/lex.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

const char* JStarTokName[] = {
#define TOKEN(tok, name) name,
#include "parse/token.def"
};

typedef struct Keyword {
    const char* name;
    size_t length;
    JStarTokType type;
} Keyword;

// clang-format off

static Keyword keywords[] = {
    {"and",      3, TOK_AND},
    {"or",       2, TOK_OR},
    {"class",    5, TOK_CLASS},
    {"else",     4, TOK_ELSE},
    {"false",    5, TOK_FALSE},
    {"for",      3, TOK_FOR},
    {"fun",      3, TOK_FUN},
    {"native",   6, TOK_NAT},
    {"if",       2, TOK_IF},
    {"elif",     4, TOK_ELIF},
    {"null",     4, TOK_NULL},
    {"return",   6, TOK_RETURN},
    {"yield",    5, TOK_YIELD},
    {"super",    5, TOK_SUPER},
    {"true",     4, TOK_TRUE},
    {"var",      3, TOK_VAR},
    {"while",    5, TOK_WHILE},
    {"import",   6, TOK_IMPORT},
    {"in",       2, TOK_IN},
    {"begin",    5, TOK_BEGIN},
    {"end",      3, TOK_END},
    {"as",       2, TOK_AS},
    {"is",       2, TOK_IS},
    {"try",      3, TOK_TRY},
    {"ensure",   6, TOK_ENSURE},
    {"except",   6, TOK_EXCEPT},
    {"raise",    5, TOK_RAISE},
    {"with",     4, TOK_WITH},
    {"continue", 8, TOK_CONTINUE},
    {"break",    5, TOK_BREAK},
    {"static",   6, TOK_STATIC},
    // sentinel
    {NULL,       0, TOK_EOF}
};

// clang-format on

static char advance(JStarLex* lex) {
    lex->current++;
    return lex->current[-1];
}

static char peekChar(JStarLex* lex) {
    return *lex->current;
}

static bool isAtEnd(JStarLex* lex) {
    return peekChar(lex) == '\0';
}

static char peekChar2(JStarLex* lex) {
    if(isAtEnd(lex)) return '\0';
    return lex->current[1];
}

static bool match(JStarLex* lex, char c) {
    if(isAtEnd(lex)) return false;
    if(peekChar(lex) == c) {
        advance(lex);
        return true;
    }
    return false;
}

void jsrInitLexer(JStarLex* lex, const char* src) {
    lex->source = src;
    lex->tokenStart = src;
    lex->current = src;
    lex->currLine = 1;

    // skip shabang if present
    if(peekChar(lex) == '#' && peekChar2(lex) == '!') {
        while(!isAtEnd(lex)) {
            if(peekChar(lex) == '\n') break;
            advance(lex);
        }
    }
}

static void skipSpacesAndComments(JStarLex* lex) {
    while(!isAtEnd(lex)) {
        switch(peekChar(lex)) {
        case '\\':
            if(peekChar2(lex) == '\n') {
                lex->currLine++;
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
                while(peekChar(lex) != '\n' && !isAtEnd(lex)) advance(lex);
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

static void makeToken(JStarLex* lex, JStarTok* tok, JStarTokType type) {
    tok->type = type;
    tok->lexeme = lex->tokenStart;
    tok->length = (int)(lex->current - lex->tokenStart);
    tok->line = lex->currLine;
}

static void eofToken(JStarLex* lex, JStarTok* tok) {
    tok->type = TOK_EOF;
    tok->lexeme = lex->current;
    tok->length = 0;
    tok->line = lex->currLine;
}

static void integer(JStarLex* lex) {
    while(isNum(peekChar(lex))) advance(lex);
}

static void number(JStarLex* lex, JStarTok* tok) {
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

static void hexNumber(JStarLex* lex, JStarTok* tok) {
    while(isHex(peekChar(lex))) advance(lex);

    if(match(lex, 'e')) {
        char c = peekChar(lex);
        if(c == '-' || c == '+') advance(lex);
        integer(lex);
    }

    makeToken(lex, tok, TOK_NUMBER);
}

static bool stringBody(JStarLex* lex, char end) {
    while(peekChar(lex) != end && !isAtEnd(lex)) {
        if(peekChar(lex) == '\n') lex->currLine++;
        if(peekChar(lex) == '\\' && peekChar2(lex) != '\0') advance(lex);
        advance(lex);
    }

    // unterminated string
    if(isAtEnd(lex)) {
        return false;
    }

    advance(lex);
    return true;
}

static void string(JStarLex* lex, char end, JStarTok* tok) {
    if(!stringBody(lex, end)) {
        makeToken(lex, tok, TOK_UNTERMINATED_STR);
    } else {
        makeToken(lex, tok, TOK_STRING);
    }
}

static void identifier(JStarLex* lex, JStarTok* tok) {
    while(isAlphaNum(peekChar(lex))) advance(lex);
    JStarTokType type = TOK_IDENTIFIER;

    // See if the identifier is a reserved word.
    size_t length = lex->current - lex->tokenStart;
    for(Keyword* keyword = keywords; keyword->name != NULL; keyword++) {
        if(length == keyword->length && memcmp(lex->tokenStart, keyword->name, length) == 0) {
            type = keyword->type;
            break;
        }
    }

    makeToken(lex, tok, type);
}

void jsrNextToken(JStarLex* lex, JStarTok* tok) {
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
    case '|':
        makeToken(lex, tok, TOK_PIPE);
        break;
    case '&':
        makeToken(lex, tok, TOK_AMPER);
        break;
    case '~':
        makeToken(lex, tok, TOK_TILDE);
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
    case '{':
        makeToken(lex, tok, TOK_LCURLY);
        break;
    case '}':
        makeToken(lex, tok, TOK_RCURLY);
        break;
    case '^':
        makeToken(lex, tok, TOK_POW);
        break;
    case '@':
        makeToken(lex, tok, TOK_AT);
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
        else if(match(lex, '>'))
            makeToken(lex, tok, TOK_ARROW);
        else
            makeToken(lex, tok, TOK_EQUAL);
        break;
    case '<':
        if(match(lex, '='))
            makeToken(lex, tok, TOK_LE);
        else if(match(lex, '<'))
            makeToken(lex, tok, TOK_LSHIFT);
        else
            makeToken(lex, tok, TOK_LT);
        break;
    case '>':
        if(match(lex, '='))
            makeToken(lex, tok, TOK_GE);
        else if(match(lex, '>'))
            makeToken(lex, tok, TOK_RSHIFT);
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
        lex->currLine++;
        break;
    default:
        makeToken(lex, tok, TOK_ERR);
        break;
    }
}

void jsrLexRewind(JStarLex* lex, JStarTok* tok) {
    if(tok->lexeme == NULL) return;
    lex->tokenStart = lex->current = tok->lexeme;
    lex->currLine = tok->line;
}
