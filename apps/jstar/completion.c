#include "completion.h"

#include <assert.h>
#include <string.h>

#include "jstar/buffer.h"
#include "jstar/jstar.h"
#include "jstar/parse/lex.h"
#include "replxx.h"

#define INDENT "    "

JSR_STATIC_ASSERT(TOK_EOF == 78, "Token count has changed, update keywords if needed");
// NULL terminated array of all J* keywords.
static const char* keywords[] = {
    "or",     "if",     "in",     "as",     "is",       "and",   "for",    "fun",    "construct",
    "var",    "end",    "try",    "else",   "elif",     "null",  "true",   "with",   "class",
    "false",  "super",  "while",  "begin",  "raise",    "break", "native", "return", "yield",
    "import", "ensure", "except", "static", "continue", NULL,
};

typedef void (*IterCB)(const char* res, void* data);

// Iterates all matching J* keywords.
static void iterKeywords(const char* ctxStart, int ctxLen, IterCB cb, void* data) {
    for(const char** kw = keywords; *kw; kw++) {
        int kwLen = strlen(*kw);
        if(kwLen > ctxLen && strncmp(ctxStart, *kw, ctxLen) == 0) {
            cb(*kw, data);
        }
    }
}

// Iterates all matching global names.
// We assert on errors as all calls should succeed on a correctly functioning J* VM.
static void iterNames(JStarVM* vm, const char* ctxStart, int ctxLen, IterCB cb, void* data) {
    bool ok = jsrGetGlobal(vm, JSR_MAIN_MODULE, "__this__");
    assert(ok);
    (void)ok;

    JStarResult res = jsrCallMethod(vm, "globals", 0);
    if(res != JSR_SUCCESS) {
        jsrPop(vm);
        return;
    }

    bool err;
    jsrPushNull(vm);

    while(jsrIter(vm, -2, -1, &err)) {
        assert(!err);

        bool ok = jsrNext(vm, -2, -1);
        assert(ok && jsrIsString(vm, -1));
        (void)ok;

        const char* global = jsrGetString(vm, -1);
        int globalLen = jsrGetStringSz(vm, -1);

        if(globalLen > ctxLen && strncmp(ctxStart, global, ctxLen) == 0) {
            cb(global, data);
        }

        jsrPop(vm);
    }

    jsrPop(vm);
    jsrPop(vm);
}

static void addHint(const char* str, void* data) {
    replxx_hints* hints = data;
    replxx_add_hint(hints, str);
}

static void hints(const char* ctx, replxx_hints* hints, int* ctxLen, ReplxxColor* color,
                  void* data) {
    JStarVM* vm = data;
    if(!*ctxLen) return;

    *color = REPLXX_COLOR_GRAY;
    const char* ctxStart = ctx + strlen(ctx) - *ctxLen;

    iterNames(vm, ctxStart, *ctxLen, addHint, hints);
    iterKeywords(ctxStart, *ctxLen, addHint, hints);
}

typedef struct {
    replxx_completions* completions;
    size_t count;
} AddCompletion;

static void addCompletion(const char* str, void* data) {
    AddCompletion* ac = data;
    replxx_add_completion(ac->completions, str);
    ac->count++;
}

static void indent(CompletionState* s, const char* ctx, size_t ctxLen,
                   replxx_completions* completions) {
    jsrBufferClear(&s->completionBuf);

    ReplxxState state;
    replxx_get_state(s->replxx, &state);

    int cursorPos = state.cursorPosition;
    int inputLen = strlen(ctx);
    int indentLen = strlen(INDENT);

    // Indent the current context up to a multiple of strlen(INDENT)
    jsrBufferAppendf(&s->completionBuf, "%.*s", ctxLen, ctx + inputLen - ctxLen);
    jsrBufferAppendf(&s->completionBuf, "%.*s", indentLen - (cursorPos % indentLen), INDENT);

    // Give the processed output to replxx for visualization
    replxx_add_completion(completions, s->completionBuf.data);
}

static void completions(const char* ctx, replxx_completions* completions, int* ctxLen, void* data) {
    CompletionState* cs = data;
    if(!*ctxLen) {
        indent(cs, ctx, *ctxLen, completions);
        return;
    }

    JStarVM* vm = cs->completionBuf.vm;
    const char* ctxStart = ctx + strlen(ctx) - *ctxLen;

    AddCompletion ac = {.completions = completions};
    iterNames(vm, ctxStart, *ctxLen, addCompletion, &ac);
    iterKeywords(ctxStart, *ctxLen, addCompletion, &ac);

    // No completions, indent line
    if(ac.count == 0) indent(cs, ctx, *ctxLen, completions);
}

void setHintCallback(Replxx* replxx, JStarVM* vm) {
    replxx_set_hint_callback(replxx, hints, vm);
}

void setCompletionCallback(Replxx* replxx, CompletionState* completionState) {
    completionState->replxx = replxx;
    replxx_set_completion_callback(replxx, completions, completionState);
}
