#include "highlighter.h"

#include <stdlib.h>

#include "jstar/parse/lex.h"

static const ReplxxColor theme[TOK_EOF] = {
    // Literals
    [TOK_NUMBER] = REPLXX_COLOR_GREEN,
    [TOK_TRUE] = REPLXX_COLOR_CYAN,
    [TOK_FALSE] = REPLXX_COLOR_CYAN,
    [TOK_STRING] = REPLXX_COLOR_BLUE,
    [TOK_UNTERMINATED_STR] = REPLXX_COLOR_BLUE,
    [TOK_NULL] = REPLXX_COLOR_MAGENTA,

// Keywords
#define KEYWORD_COLOR REPLXX_COLOR_MAGENTA
    [TOK_VAR] = KEYWORD_COLOR,
    [TOK_STATIC] = KEYWORD_COLOR,
    [TOK_CLASS] = KEYWORD_COLOR,
    [TOK_FUN] = KEYWORD_COLOR,
    [TOK_NAT] = KEYWORD_COLOR,
    [TOK_END] = KEYWORD_COLOR,

    // Punctuation
    [TOK_SEMICOLON] = REPLXX_COLOR_RED,

    // Error
    [TOK_ERR] = REPLXX_COLOR_RED,
};

void highlighter(const char* input, ReplxxColor* colors, int size, void* userData) {
    Replxx* replxx = userData;
    
    JStarLex lex;
    JStarTok tok;

    jsrInitLexer(&lex, input);
    jsrNextToken(&lex, &tok);

    while(tok.type != TOK_EOF && tok.type != TOK_NEWLINE) {
        ReplxxColor themeColor = theme[tok.type];
        if(themeColor) {
            size_t startOffset = tok.lexeme - input;
            for(size_t i = startOffset; i < startOffset + tok.length; i++) {
                colors[i] = themeColor;
            }
        }
        jsrNextToken(&lex, &tok);
    }

    if((size > 0) && (input[size - 1] == '(')) {
        replxx_emulate_key_press(replxx, ')');
        replxx_emulate_key_press(replxx, REPLXX_KEY_LEFT);
    }
}