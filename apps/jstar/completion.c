#include "completion.h"

#include <jstar/jstar.h>
#include <jstar/parse/lex.h>
#include <replxx.h>
#include <string.h>

#include "extlib.h"

typedef void (*IterCB)(const char* res, void* data);

typedef struct {
    replxx_completions* completions;
    size_t count;
} AddCompletion;

JSR_STATIC_ASSERT(TOK_EOF == 78, "Token count has changed; update `keywords` if needed");
static const char* keywords[] = {
    "or",     "if",     "in",     "as",     "is",       "and",   "for",    "fun",    "construct",
    "var",    "end",    "try",    "else",   "elif",     "null",  "true",   "with",   "class",
    "false",  "super",  "while",  "begin",  "raise",    "break", "native", "return", "yield",
    "import", "ensure", "except", "static", "continue", NULL,
};

static void iterKeywords(const char* ctxStart, int ctxLen, IterCB cb, void* data) {
    for(const char** kw = keywords; *kw; kw++) {
        size_t kwLen = strlen(*kw);
        if((int)kwLen > ctxLen && strncmp(ctxStart, *kw, ctxLen) == 0) {
            cb(*kw, data);
        }
    }
}

static void iterNames(JStarVM* vm, const char* ctxStart, int ctxLen, IterCB cb, void* data) {
    bool ok = jsrGetGlobal(vm, JSR_MAIN_MODULE, "__this__");
    JSR_ASSERT(ok, "`jsrGetGlobal(vm, JSR_MAIN_MODULE, \"__this__\")` failed");

    if(!jsrCallMethod(vm, "globals", 0)) {
        jsrPop(vm);
        return;
    }

    bool err;
    jsrPushNull(vm);

    while(jsrIter(vm, -2, -1, &err)) {
        JSR_ASSERT(!err, "`jsrIter(vm, -2, -1, &err)` failed");

        bool ok = jsrNext(vm, -2, -1);
        JSR_ASSERT(ok && jsrIsString(vm, -1), "`jsrNext(vm, -2, -1)` failed");

        const char* global = jsrGetString(vm, -1);
        size_t globalLen = jsrGetStringSz(vm, -1);

        if((int)globalLen > ctxLen && strncmp(ctxStart, global, ctxLen) == 0) {
            cb(global, data);
        }

        jsrPop(vm);
    }

    jsrPopN(vm, 2);
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

static void indent(Replxx* replxx, const char* ctx, int ctxLen, replxx_completions* completions) {
    ReplxxState state;
    replxx_get_state(replxx, &state);

    // Indent the current context up to a multiple of INDENT_LEN.
    int spaces = INDENT_LEN - (state.cursorPosition % INDENT_LEN);
    const char* ctxStart = ctx + strlen(ctx) - ctxLen;

    void* chk;
    defer_loop(chk = temp_checkpoint(), temp_rewind(chk)) {
        StringBuffer sb = {.allocator = &temp_allocator.base};
        sb_appendf(&sb, "%.*s", ctxLen, ctxStart);
        sb_appendf(&sb, "%.*s", (int)spaces, INDENT);
        sb_append_char(&sb, '\0');
        replxx_add_completion(completions, sb.items);
    }
}

static ReplxxActionResult deindent(int code, void* userData) {
    Replxx* replxx = userData;

    ReplxxState state;
    replxx_get_state(replxx, &state);
    int cursor = state.cursorPosition;

    int leading = 0;
    while(state.text[leading] == ' ') {
        ++leading;
    }

    if(cursor > 0 && cursor <= leading) {
        int remove = (cursor - 1) % INDENT_LEN + 1;

        ReplxxActionResult result = REPLXX_ACTION_RESULT_CONTINUE;
        for(int i = 0; i < remove; ++i) {
            result = replxx_invoke(replxx, REPLXX_ACTION_DELETE_CHARACTER_LEFT_OF_CURSOR, code);
        }

        return result;
    }

    return replxx_invoke(replxx, REPLXX_ACTION_DELETE_CHARACTER_LEFT_OF_CURSOR, code);
}

static void addCompletion(const char* str, void* data) {
    AddCompletion* ac = data;
    replxx_add_completion(ac->completions, str);
    ac->count++;
}

static void completions(const char* ctx, replxx_completions* completions, int* ctxLen, void* data) {
    CompletionState* cs = data;
    AddCompletion ac = {.completions = completions};

    if(*ctxLen) {
        const char* ctxStart = ctx + strlen(ctx) - *ctxLen;
        iterNames(cs->vm, ctxStart, *ctxLen, addCompletion, &ac);
        iterKeywords(ctxStart, *ctxLen, addCompletion, &ac);
    }

    // No completions, indent line
    if(ac.count == 0) {
        indent(cs->replxx, ctx, *ctxLen, completions);
    }
}

void setHintCallback(Replxx* replxx, JStarVM* vm) {
    replxx_set_hint_callback(replxx, hints, vm);
}

void setCompletionCallback(CompletionState* cs) {
    replxx_set_completion_callback(cs->replxx, completions, cs);
    replxx_bind_key(cs->replxx, REPLXX_KEY_BACKSPACE, deindent, cs->replxx);
}
