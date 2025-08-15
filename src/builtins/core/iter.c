#include "iter.h"

#include <stdbool.h>
#include <stddef.h>

#include "buffer.h"
#include "jstar.h"

// class Iterable
JSR_NATIVE(jsr_core_iter_join) {
    JSR_CHECK(String, 2, "sep");

    const char* sep = jsrGetString(vm, 2);
    size_t sepLen = jsrGetStringSz(vm, 2);

    JStarBuffer joined;
    jsrBufferInit(vm, &joined);

    JSR_FOREACH(1) {
        if(err) goto error;
        if(!jsrIsString(vm, -1)) {
            if((jsrCallMethod(vm, "__string__", 0) != JSR_SUCCESS)) goto error;
            if(!jsrIsString(vm, -1)) {
                jsrRaise(vm, "TypeException", "s.__string__() didn't return a String");
                goto error;
            }
        }
        jsrBufferAppend(&joined, jsrGetString(vm, -1), jsrGetStringSz(vm, -1));
        jsrBufferAppend(&joined, sep, sepLen);
        jsrPop(vm);
    }

    if(joined.size > 0) {
        jsrBufferTrunc(&joined, joined.size - sepLen);
    }

    jsrBufferPush(&joined);
    return true;

error:
    jsrBufferFree(&joined);
    return false;
}
// end
