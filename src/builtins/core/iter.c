#include "iter.h"

// class Iterable
JSR_NATIVE(jsr_core_iter_Iterable_join) {
    JSR_CHECK(String, 1, "sep");

    JStarBuffer joined;
    jsrBufferInit(vm, &joined);

    JSR_FOREACH(0, {
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
        jsrBufferAppend(&joined, jsrGetString(vm, 1), jsrGetStringSz(vm, 1));
        jsrPop(vm);
    },
    jsrBufferFree(&joined))

    if(joined.size > 0) {
        jsrBufferTrunc(&joined, joined.size - jsrGetStringSz(vm, 1));
    }

    jsrBufferPush(&joined);
    return true;
}
// end
