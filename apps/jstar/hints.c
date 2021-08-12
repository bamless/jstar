#include "hints.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "jstar/jstar.h"

// NULL terminated array of all J* keywords.
static const char* keywords[] = {
    "or",     "if",     "in",     "as",     "is",     "and",      "for",   "fun",
    "var",    "end",    "try",    "else",   "elif",   "null",     "true",  "with",
    "class",  "false",  "super",  "while",  "begin",  "raise",    "break", "native",
    "return", "import", "ensure", "except", "static", "continue", NULL,
};

// Add all matching keywords to the hints array.
static void hintKeywords(const char* ctxStart, int ctxLen, replxx_hints* hints) {
    for(const char** kw = keywords; *kw; kw++) {
        int kwLen = strlen(*kw);
        if(kwLen > ctxLen && strncmp(ctxStart, *kw, ctxLen) == 0) {
            replxx_add_hint(hints, *kw);
        }
    }
}

// Add all matching global names to the hints array.
// We assert on all errors as all calls should succeed on a correctly functioning J* VM.
static void hintNames(JStarVM* vm, const char* ctxStart, int ctxLen, replxx_hints* hints) {
    bool ok = jsrGetGlobal(vm, JSR_MAIN_MODULE, "__this__");
    assert(ok);

    JStarResult res = jsrCallMethod(vm, "globals", 0);
    assert(res == JSR_SUCCESS);
    (void) res;

    bool err;
    jsrPushNull(vm);
    while(jsrIter(vm, -2, -1, &err)) {
        assert(!err);
        
        ok = jsrNext(vm, -2, -1);
        assert(ok);

        assert(jsrIsString(vm, -1));

        const char* global = jsrGetString(vm, -1);
        int globalLen = jsrGetStringSz(vm, -1);

        if(globalLen > ctxLen && strncmp(ctxStart, global, ctxLen) == 0) {
            replxx_add_hint(hints, global);
        }

        jsrPop(vm);
    }

    jsrPop(vm);
    jsrPop(vm);
}

void hints(const char* input, replxx_hints* hints, int* ctxLen, ReplxxColor* color, void* ud) {
    if(!*ctxLen) return;

    JStarVM* vm = ud;
    const char* ctxStart = input + strlen(input) - *ctxLen;

    hintNames(vm, ctxStart, *ctxLen, hints);
    hintKeywords(ctxStart, *ctxLen, hints);

    *color = REPLXX_COLOR_GRAY;
}