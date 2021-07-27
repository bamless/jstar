#include "hints.h"

#include <stdlib.h>
#include <string.h>

static const char* keywords[] = {
    "or",     "if",     "in",     "as",     "is",     "and",      "for",   "fun",
    "var",    "end",    "try",    "else",   "elif",   "null",     "true",  "with",
    "class",  "false",  "super",  "while",  "begin",  "raise",    "break", "native",
    "return", "import", "ensure", "except", "static", "continue", NULL,
};

void hints(const char* input, replxx_hints* hints, int* ctxLen, ReplxxColor* c, void* ud) {
    if(!*ctxLen) return;

    const char* ctxStart = input + strlen(input) - *ctxLen;
    for(const char** kw = keywords; *kw; kw++) {
        int kwLen = strlen(*kw);
        if(kwLen > *ctxLen && strncmp(ctxStart, *kw, *ctxLen) == 0) {
            replxx_add_hint(hints, *kw);
        }
    }

    *c = REPLXX_COLOR_GRAY;
}