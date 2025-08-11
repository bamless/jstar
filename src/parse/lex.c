#include "parse/lex.h"

#include <stdbool.h>
#include <string.h>
#include "conf.h"

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

JSR_STATIC_ASSERT(TOK_EOF == 78, "Number of token changed, update keyword if necessary");
static Keyword keywords[] = {
#define STR(s) (s), sizeof(s) - 1
    {STR("and"),       TOK_AND},
    {STR("or"),        TOK_OR},
    {STR("class"),     TOK_CLASS},
    {STR("else"),      TOK_ELSE},
    {STR("false"),     TOK_FALSE},
    {STR("for"),       TOK_FOR},
    {STR("fun"),       TOK_FUN},
    {STR("construct"), TOK_CTOR},
    {STR("native"),    TOK_NAT},
    {STR("if"),        TOK_IF},
    {STR("elif"),      TOK_ELIF},
    {STR("null"),      TOK_NULL},
    {STR("return"),    TOK_RETURN},
    {STR("yield"),     TOK_YIELD},
    {STR("super"),     TOK_SUPER},
    {STR("true"),      TOK_TRUE},
    {STR("var"),       TOK_VAR},
    {STR("while"),     TOK_WHILE},
    {STR("import"),    TOK_IMPORT},
    {STR("in"),        TOK_IN},
    {STR("begin"),     TOK_BEGIN},
    {STR("end"),       TOK_END},
    {STR("as"),        TOK_AS},
    {STR("is"),        TOK_IS},
    {STR("try"),       TOK_TRY},
    {STR("ensure"),    TOK_ENSURE},
    {STR("except"),    TOK_EXCEPT},
    {STR("raise"),     TOK_RAISE},
    {STR("with"),      TOK_WITH},
    {STR("continue"),  TOK_CONTINUE},
    {STR("break"),     TOK_BREAK},
    {STR("static"),    TOK_STATIC},
    // sentinel
    {NULL, 0,          TOK_EOF}
};

// clang-format on

static char advance(JStarLex* lex) {
    lex->current++;
    return lex->current[-1];
}

static bool isAtEnd(JStarLex* lex) {
    return (size_t)(lex->current - lex->source) == lex->sourceLen;
}

static char peekChar(JStarLex* lex) {
    if(isAtEnd(lex)) return '\0';
    return *lex->current;
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

void jsrInitLexer(JStarLex* lex, const char* src, size_t len) {
    lex->source = src;
    lex->sourceLen = len;
    lex->lineStart = src;
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
                advance(lex);
                advance(lex);
                lex->currLine++;
                lex->lineStart = lex->current;
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
    tok->length = (int)(lex->current - lex->tokenStart);
    tok->lexeme = lex->tokenStart;
    tok->loc.line = lex->currLine;
    tok->loc.col = (int)(lex->tokenStart - lex->lineStart) + 1;
}

static void eofToken(JStarLex* lex, JStarTok* tok) {
    tok->type = TOK_EOF;
    tok->length = 0;
    tok->lexeme = lex->current;
    tok->loc.line = lex->currLine;
    tok->loc.col = (int)(lex->current - lex->lineStart) + 1;
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

static void string(JStarLex* lex, char end, JStarTok* tok) {
    const char* lineStart = lex->current;
    int currLine = lex->currLine;

    while(peekChar(lex) != end && !isAtEnd(lex)) {
        if(peekChar(lex) == '\n') {
            lineStart = lex->current + 1;
            currLine++;
        }
        if(peekChar(lex) == '\\' && peekChar2(lex) != '\0') advance(lex);
        advance(lex);
    }

    if(isAtEnd(lex)) {
        makeToken(lex, tok, TOK_UNTERMINATED_STR);
    } else {
        advance(lex);
        makeToken(lex, tok, TOK_STRING);
    }

    lex->lineStart = lineStart;
    lex->currLine = currLine;
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

bool jsrNextToken(JStarLex* lex, JStarTok* tok) {
    skipSpacesAndComments(lex);

    if(isAtEnd(lex)) {
        eofToken(lex, tok);
        return false;
    }

    lex->tokenStart = lex->current;
    char c = advance(lex);

    if(c == '0' && match(lex, 'x')) {
        hexNumber(lex, tok);
        return true;
    }
    if(isNum(c) || (c == '.' && isNum(peekChar(lex)))) {
        number(lex, tok);
        return true;
    }
    if(isAlpha(c)) {
        identifier(lex, tok);
        return true;
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
            makeToken(lex, tok, TOK_ELLIPSIS);
        } else {
            makeToken(lex, tok, TOK_DOT);
        }
        break;
    case '-':
        if(match(lex, '=')) makeToken(lex, tok, TOK_MINUS_EQ);
        else makeToken(lex, tok, TOK_MINUS);
        break;
    case '+':
        if(match(lex, '=')) makeToken(lex, tok, TOK_PLUS_EQ);
        else makeToken(lex, tok, TOK_PLUS);
        break;
    case '/':
        if(match(lex, '=')) makeToken(lex, tok, TOK_DIV_EQ);
        else makeToken(lex, tok, TOK_DIV);
        break;
    case '*':
        if(match(lex, '=')) makeToken(lex, tok, TOK_MULT_EQ);
        else makeToken(lex, tok, TOK_MULT);
        break;
    case '%':
        if(match(lex, '=')) makeToken(lex, tok, TOK_MOD_EQ);
        else makeToken(lex, tok, TOK_MOD);
        break;
    case '!':
        if(match(lex, '=')) makeToken(lex, tok, TOK_BANG_EQ);
        else makeToken(lex, tok, TOK_BANG);
        break;
    case '=':
        if(match(lex, '=')) makeToken(lex, tok, TOK_EQUAL_EQUAL);
        else if(match(lex, '>')) makeToken(lex, tok, TOK_ARROW);
        else makeToken(lex, tok, TOK_EQUAL);
        break;
    case '<':
        if(match(lex, '=')) makeToken(lex, tok, TOK_LE);
        else if(match(lex, '<')) makeToken(lex, tok, TOK_LSHIFT);
        else makeToken(lex, tok, TOK_LT);
        break;
    case '>':
        if(match(lex, '=')) makeToken(lex, tok, TOK_GE);
        else if(match(lex, '>')) makeToken(lex, tok, TOK_RSHIFT);
        else makeToken(lex, tok, TOK_GT);
        break;
    case '#':
        if(match(lex, '#')) makeToken(lex, tok, TOK_HASH_HASH);
        else makeToken(lex, tok, TOK_HASH);
        break;
    case '\n':
        makeToken(lex, tok, TOK_NEWLINE);
        lex->currLine++;
        lex->lineStart = lex->current;
        break;
    default:
        makeToken(lex, tok, TOK_ERR);
        break;
    }
    return true;
}

void jsrLexRewind(JStarLex* lex, const JStarTok* tok) {
    if(tok->lexeme == NULL) return;
    lex->lineStart = tok->lexeme - (tok->loc.col - 1);
    lex->currLine = tok->loc.line;
    lex->tokenStart = lex->current = tok->lexeme;
}
