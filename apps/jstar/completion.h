#ifndef HINTS_H
#define HINTS_H

#include <extlib.h>
#include <jstar/jstar.h>
#include <replxx.h>

typedef struct {
    JStarVM* vm;
    Replxx* replxx;
    StringBuffer completionBuf;
} CompletionState;

// Sets replxx hints callback with global name resolution support
void setHintCallback(Replxx* replxx, JStarVM* vm);
// Sets replxx auto-completion callback with global name resolution and indentation support
void setCompletionCallback(Replxx* replxx, CompletionState* completionState);

#endif
