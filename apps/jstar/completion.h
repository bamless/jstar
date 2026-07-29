#ifndef HINTS_H
#define HINTS_H

#include <jstar/jstar.h>
#include <replxx.h>

#define INDENT     "    "
#define INDENT_LEN (sizeof(INDENT) - 1)

typedef struct {
    Replxx* replxx;
    JStarVM* vm;
} CompletionState;

// Sets replxx hints callback with global name resolution support
void setHintCallback(Replxx* replxx, JStarVM* vm);
// Sets replxx auto-completion callback with global name resolution and indentation support
void setCompletionCallback(CompletionState* cs);

#endif
