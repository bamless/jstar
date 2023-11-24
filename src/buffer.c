#include "buffer.h"

#include <stdio.h>
#include <string.h>

#include "gc.h"
#include "jstar.h"
#include "object.h"
#include "util.h"
#include "value.h"
#include "vm.h"

#define JSR_BUF_DEFAULT_CAPACITY 16

static void bufferGrow(JStarBuffer* b, size_t len) {
    size_t newSize = b->capacity;
    while(newSize < b->size + len) {
        newSize <<= 1;
    }
    char* newData = gcAlloc(b->vm, b->data, b->capacity, newSize);
    b->capacity = newSize;
    b->data = newData;
}

void jsrBufferInit(JStarVM* vm, JStarBuffer* b) {
    jsrBufferInitCapacity(vm, b, JSR_BUF_DEFAULT_CAPACITY);
}

void jsrBufferInitCapacity(JStarVM* vm, JStarBuffer* b, size_t capacity) {
    if(capacity < JSR_BUF_DEFAULT_CAPACITY) capacity = JSR_BUF_DEFAULT_CAPACITY;
    b->vm = vm;
    b->capacity = capacity;
    b->size = 0;
    b->data = GC_ALLOC(vm, capacity);
}

void jsrBufferAppend(JStarBuffer* b, const char* str, size_t len) {
    if(b->size + len >= b->capacity) {
        bufferGrow(b, len + 1);  // the >= and the +1 are for the terminating NUL
    }
    memcpy(&b->data[b->size], str, len);
    b->size += len;
    b->data[b->size] = '\0';
}

void jsrBufferAppendStr(JStarBuffer* b, const char* str) {
    jsrBufferAppend(b, str, strlen(str));
}

void jsrBufferAppendvf(JStarBuffer* b, const char* fmt, va_list ap) {
    size_t availableSpace = b->capacity - b->size;

    va_list cpy;
    va_copy(cpy, ap);
    size_t written = vsnprintf(&b->data[b->size], availableSpace, fmt, cpy);
    va_end(cpy);

    // Not enough space, need to grow and retry
    if(written >= availableSpace) {
        bufferGrow(b, written + 1);
        availableSpace = b->capacity - b->size;
        va_copy(cpy, ap);
        written = vsnprintf(&b->data[b->size], availableSpace, fmt, cpy);
        JSR_ASSERT(written < availableSpace, "Buffer still to small");
        va_end(cpy);
    }

    b->size += written;
}

void jsrBufferAppendf(JStarBuffer* b, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    jsrBufferAppendvf(b, fmt, ap);
    va_end(ap);
}

void jsrBufferTrunc(JStarBuffer* b, size_t len) {
    if(len >= b->size) return;
    b->size = len;
    b->data[len] = '\0';
}

void jsrBufferCut(JStarBuffer* b, size_t len) {
    if(len == 0 || len > b->size) return;
    memmove(b->data, b->data + len, b->size - len);
    b->size -= len;
    b->data[b->size] = '\0';
}

void jsrBufferReplaceChar(JStarBuffer* b, size_t start, char c, char r) {
    for(size_t i = start; i < b->size; i++) {
        if(b->data[i] == c) {
            b->data[i] = r;
        }
    }
}

void jsrBufferPrepend(JStarBuffer* b, const char* str, size_t len) {
    if(b->size + len >= b->capacity) {
        bufferGrow(b, len + 1);  // the >= and the +1 are for the terminating NUL
    }
    memmove(b->data + len, b->data, b->size);
    memcpy(b->data, str, len);
    b->size += len;
    b->data[b->size] = '\0';
}

void jsrBufferPrependStr(JStarBuffer* b, const char* str) {
    jsrBufferPrepend(b, str, strlen(str));
}

void jsrBufferAppendChar(JStarBuffer* b, char c) {
    if(b->size + 1 >= b->capacity) bufferGrow(b, 2);
    b->data[b->size++] = c;
    b->data[b->size] = '\0';
}

void jsrBufferShrinkToFit(JStarBuffer* b) {
    b->data = gcAlloc(b->vm, b->data, b->capacity, b->size);
    b->capacity = b->size;
}

void jsrBufferClear(JStarBuffer* b) {
    b->size = 0;
    b->data[0] = '\0';
}

void jsrBufferPush(JStarBuffer* b) {
    JStarVM* vm = b->vm;
    push(vm, OBJ_VAL(jsrBufferToString(b)));
}

void jsrBufferFree(JStarBuffer* b) {
    if(b->data != NULL) {
        GC_FREE_ARRAY(b->vm, char, b->data, b->capacity);
    }
    *b = (JStarBuffer){0};
}