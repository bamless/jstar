#include "highlighter.h"

#include <stdlib.h>
#include <string.h>

#include "jstar/conf.h"
#include "jstar/parse/lex.h"
#include "replxx.h"

// -----------------------------------------------------------------------------
// COLOR THEME DEFINITION
// -----------------------------------------------------------------------------

#define CLASS_NAME_COLOR      REPLXX_COLOR_YELLOW
#define IDENTIFIER_CALL_COLOR REPLXX_COLOR_YELLOW

JSR_STATIC_ASSERT(TOK_EOF == 78, "Token count has changed, update highlighter if needed");
static const ReplxxColor theme[TOK_EOF] = {
// Keywords
#define KEYWORD_COLOR REPLXX_COLOR_BLUE
    [TOK_AND] = KEYWORD_COLOR,
    [TOK_OR] = KEYWORD_COLOR,
    [TOK_CLASS] = KEYWORD_COLOR,
    [TOK_ELSE] = KEYWORD_COLOR,
    [TOK_FOR] = KEYWORD_COLOR,
    [TOK_FUN] = KEYWORD_COLOR,
    [TOK_CTOR] = KEYWORD_COLOR,
    [TOK_NAT] = KEYWORD_COLOR,
    [TOK_IF] = KEYWORD_COLOR,
    [TOK_ELIF] = KEYWORD_COLOR,
    [TOK_RETURN] = KEYWORD_COLOR,
    [TOK_YIELD] = KEYWORD_COLOR,
    [TOK_WHILE] = KEYWORD_COLOR,
    [TOK_IMPORT] = KEYWORD_COLOR,
    [TOK_IN] = KEYWORD_COLOR,
    [TOK_BEGIN] = KEYWORD_COLOR,
    [TOK_END] = KEYWORD_COLOR,
    [TOK_AS] = KEYWORD_COLOR,
    [TOK_IS] = KEYWORD_COLOR,
    [TOK_TRY] = KEYWORD_COLOR,
    [TOK_ENSURE] = KEYWORD_COLOR,
    [TOK_EXCEPT] = KEYWORD_COLOR,
    [TOK_RAISE] = KEYWORD_COLOR,
    [TOK_WITH] = KEYWORD_COLOR,
    [TOK_CONTINUE] = KEYWORD_COLOR,
    [TOK_BREAK] = KEYWORD_COLOR,

// `this` and `super` keywords
#define METHOD_KEYWORD_COLOR REPLXX_COLOR_BLUE
    [TOK_SUPER] = METHOD_KEYWORD_COLOR,

// Storage keywords
#define STORAGE_KEYWORD_COLOR REPLXX_COLOR_BLUE
    [TOK_VAR] = STORAGE_KEYWORD_COLOR,
    [TOK_STATIC] = STORAGE_KEYWORD_COLOR,

// Punctuation
#define PUNCTUATION_COLOR REPLXX_COLOR_DEFAULT
    [TOK_SEMICOLON] = PUNCTUATION_COLOR,
    [TOK_PIPE] = PUNCTUATION_COLOR,
    [TOK_LPAREN] = PUNCTUATION_COLOR,
    [TOK_RPAREN] = PUNCTUATION_COLOR,
    [TOK_LSQUARE] = PUNCTUATION_COLOR,
    [TOK_RSQUARE] = PUNCTUATION_COLOR,
    [TOK_LCURLY] = PUNCTUATION_COLOR,
    [TOK_RCURLY] = PUNCTUATION_COLOR,
    [TOK_COLON] = PUNCTUATION_COLOR,
    [TOK_COMMA] = PUNCTUATION_COLOR,
    [TOK_DOT] = PUNCTUATION_COLOR,

    // Literals
    [TOK_NUMBER] = REPLXX_COLOR_GREEN,
    [TOK_TRUE] = REPLXX_COLOR_CYAN,
    [TOK_FALSE] = REPLXX_COLOR_CYAN,
    [TOK_STRING] = REPLXX_COLOR_BLUE,
    [TOK_UNTERMINATED_STR] = REPLXX_COLOR_BLUE,
    [TOK_NULL] = REPLXX_COLOR_MAGENTA,

    // Misc
    [TOK_ARROW] = REPLXX_COLOR_RED,
    [TOK_AT] = REPLXX_COLOR_RED,

    // Error
    [TOK_ERR] = REPLXX_COLOR_RED,
};

// -----------------------------------------------------------------------------
// HIGHLIGHTER FUNCTION
// -----------------------------------------------------------------------------

int utf8strCodepointLen(const char* s, int size) {
    int codepointLen = 0;
    int i = 0;
    while(i < size) {
        unsigned char c = s[i];
        if(c <= 0x7F) {
            // 1-byte (ASCII)
            i += 1;
        } else if((c & 0xE0) == 0xC0) {
            // Expecting 2-byte sequence
            if(i + 1 >= size) return 0;
            unsigned char c2 = s[i + 1];
            if((c2 & 0xC0) != 0x80) return 0;
            i += 2;
        } else if((c & 0xF0) == 0xE0) {
            // Expecting 3-byte sequence
            if(i + 2 >= size) return 0;
            unsigned char c2 = s[i + 1], c3 = s[i + 2];
            if((c2 & 0xC0) != 0x80 || (c3 & 0xC0) != 0x80) return 0;
            i += 3;
        } else if((c & 0xF8) == 0xF0) {
            // Expecting 4-byte sequence
            if(i + 3 >= size) return 0;
            unsigned char c2 = s[i + 1], c3 = s[i + 2], c4 = s[i + 3];
            if((c2 & 0xC0) != 0x80 || (c3 & 0xC0) != 0x80 || (c4 & 0xC0) != 0x80) return 0;
            i += 4;
        } else {
            // Invalid first byte
            return 0;
        }
        codepointLen++;
    }
    return codepointLen;
}

static void setTokColor(const char* in, const JStarTok* tok, ReplxxColor color, ReplxxColor* out) {
    size_t utf8Len = utf8strCodepointLen(tok->lexeme, tok->length);
    size_t startOffset = tok->lexeme - in;
    for(size_t i = startOffset; i < startOffset + utf8Len; i++) {
        out[i] = color;
    }
}

static void highlighter(const char* input, ReplxxColor* colors, int size, void* userData) {
    JStarLex lex;
    jsrInitLexer(&lex, input, strlen(input));

    JStarTok prev, tok;
    jsrNextToken(&lex, &tok);
    prev = tok;

    while(tok.type != TOK_EOF) {
        if(tok.type == TOK_LPAREN && prev.type == TOK_IDENTIFIER) {
            setTokColor(input, &prev, IDENTIFIER_CALL_COLOR, colors);
        }

        ReplxxColor themeColor = theme[tok.type];

        if(tok.type == TOK_IDENTIFIER && (prev.type == TOK_CLASS || prev.type == TOK_IS)) {
            themeColor = CLASS_NAME_COLOR;
        }
        if(tok.type == TOK_IDENTIFIER && tok.length == strlen("this") &&
           strncmp(tok.lexeme, "this", (size_t)tok.length) == 0) {
            themeColor = METHOD_KEYWORD_COLOR;
        }

        if(themeColor) {
            setTokColor(input, &tok, themeColor, colors);
        }

        prev = tok;
        jsrNextToken(&lex, &tok);
    }
}

void setHighlighterCallback(Replxx* replxx) {
    replxx_set_highlighter_callback(replxx, highlighter, replxx);
}
