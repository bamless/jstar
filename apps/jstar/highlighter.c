#include "highlighter.h"

#include <stdlib.h>
#include <string.h>

#include "jstar/parse/lex.h"

// -----------------------------------------------------------------------------
// COLOR THEME DEFINITION
// -----------------------------------------------------------------------------

#define CLASS_NAME_COLOR      REPLXX_COLOR_YELLOW
#define IDENTIFIER_CALL_COLOR REPLXX_COLOR_YELLOW

static const ReplxxColor theme[TOK_EOF] = {
    // Keywords
    #define KEYWORD_COLOR REPLXX_COLOR_BLUE
    [TOK_AND] = KEYWORD_COLOR,
    [TOK_OR] = KEYWORD_COLOR,
    [TOK_CLASS] = KEYWORD_COLOR,
    [TOK_ELSE] = KEYWORD_COLOR,
    [TOK_FOR] = KEYWORD_COLOR,
    [TOK_FUN] = KEYWORD_COLOR,
    [TOK_NAT] = KEYWORD_COLOR,
    [TOK_IF] = KEYWORD_COLOR,
    [TOK_ELIF] = KEYWORD_COLOR,
    [TOK_RETURN] = KEYWORD_COLOR,
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
    #define PUNCTUATION_COLOR REPLXX_COLOR_GRAY
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

    // Error
    [TOK_ERR] = REPLXX_COLOR_RED,
};

// -----------------------------------------------------------------------------
// HIGHLIGHTER FUNCTION
// -----------------------------------------------------------------------------

static void setTokColor(const char* in, const JStarTok* tok, ReplxxColor color, ReplxxColor* out) {
    size_t startOffset = tok->lexeme - in;
    for(size_t i = startOffset; i < startOffset + tok->length; i++) {
        out[i] = color;
    }
}

void highlighter(const char* input, ReplxxColor* colors, int size, void* userData) {
    JStarLex lex;
    jsrInitLexer(&lex, input);

    JStarTok prev, tok;
    jsrNextToken(&lex, &tok);
    prev = tok;

    while(tok.type != TOK_EOF && tok.type != TOK_NEWLINE) {
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