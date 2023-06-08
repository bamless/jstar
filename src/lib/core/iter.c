#include "iter.h"

// class Iterable
JSR_NATIVE(jsr_core_iter_join) {
    JSR_CHECK(String, 2, "sep");

    const char* sep = jsrGetString(vm, 2);
    size_t sepLen = jsrGetStringSz(vm, 2);

    JStarBuffer joined;
    jsrBufferInit(vm, &joined);

    JSR_FOREACH(1, {
        if(!jsrIsString(vm, -1)) {
            if((jsrCallMethod(vm, "__string__", 0) != JSR_SUCCESS)) {
                jsrBufferFree(&joined);
                return false;
            }
            if(!jsrIsString(vm, -1)) {
                jsrBufferFree(&joined);
                JSR_RAISE(vm, "TypeException", "s.__string__() didn't return a String");
            }
        }
        jsrBufferAppend(&joined, jsrGetString(vm, -1), jsrGetStringSz(vm, -1));
        jsrBufferAppend(&joined, sep, sepLen);
        jsrPop(vm);
    },
    jsrBufferFree(&joined))

    if(joined.size > 0) {
        jsrBufferTrunc(&joined, joined.size - sepLen);
    }

    jsrBufferPush(&joined);
    return true;
}
// end
