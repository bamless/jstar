#ifndef JSTAR_BUFFER_H
#define JSTAR_BUFFER_H

#include <stdarg.h>
#include <stdlib.h>

#include "conf.h"

struct JStarVM;

// Dynamic Buffer that holds memory allocated by the J* garbage collector.
// This memory is owned by J*, but cannot be collected until the buffer
// is pushed on the stack using the jsrBufferPush method.
// Can be used for efficient creation of Strings in the native API or
// to efficiently store binary data.
typedef struct JStarBuffer {
    struct JStarVM* vm;
    size_t capacity, size;
    char* data;
} JStarBuffer;

// -----------------------------------------------------------------------------
// JSTARBUFFER API
// -----------------------------------------------------------------------------

/*
 * The following functions are safe to call prior to runtime initialization, with the obvious
 * exception of `jsrBufferPush`.
 */

JSTAR_API void jsrBufferInit(struct JStarVM* vm, JStarBuffer* b);
JSTAR_API void jsrBufferInitCapacity(struct JStarVM* vm, JStarBuffer* b, size_t capacity);
JSTAR_API void jsrBufferAppend(JStarBuffer* b, const char* str, size_t len);
JSTAR_API void jsrBufferAppendStr(JStarBuffer* b, const char* str);
JSTAR_API void jsrBufferAppendvf(JStarBuffer* b, const char* fmt, va_list ap);
JSTAR_API void jsrBufferAppendf(JStarBuffer* b, const char* fmt, ...);
JSTAR_API void jsrBufferTrunc(JStarBuffer* b, size_t len);
JSTAR_API void jsrBufferCut(JStarBuffer* b, size_t len);
JSTAR_API void jsrBufferReplaceChar(JStarBuffer* b, size_t start, char c, char r);
JSTAR_API void jsrBufferPrepend(JStarBuffer* b, const char* str, size_t len);
JSTAR_API void jsrBufferPrependStr(JStarBuffer* b, const char* str);
JSTAR_API void jsrBufferAppendChar(JStarBuffer* b, char c);
JSTAR_API void jsrBufferShrinkToFit(JStarBuffer* b);
JSTAR_API void jsrBufferClear(JStarBuffer* b);

// If not pushed with jsrBufferPush the buffer must be freed
JSTAR_API void jsrBufferFree(JStarBuffer* b);

// Once the buffer is pushed on the J* stack it becomes a String and can't be modified further
// One can reuse the JStarBuffer struct by re-initializing it using the jsrBufferInit method.
JSTAR_API void jsrBufferPush(JStarBuffer* b);

#endif  // BUFFER_H
