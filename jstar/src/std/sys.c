#include "sys.h"

#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "gc.h"
#include "value.h"
#include "vm.h"

#if defined(JSTAR_POSIX)
    #define USE_POPEN
    #include <stdio.h>
#elif defined(JSTAR_WINDOWS)
    #define USE_POPEN
    #include <stdio.h>
    #define popen  _popen
    #define pclose _pclose
#endif

#if defined(JSTAR_WINDOWS)
    #define PLATFORM "Windows"
#elif defined(JSTAR_LINUX)
    #define PLATFORM "Linux"
#elif defined(JSTAR_MACOS)
    #define PLATFORM "OS X"
#elif defined(JSTAR_IOS)
    #define PLATFORM "iOS"
#elif defined(__ANDROID__)
    #define PLATFORM "Android"
#elif defined(__FreeBSD__)
    #define PLATFORM "FreeBSD"
#else
    #define PLATFORM "Unknown"
#endif

JSR_NATIVE(jsr_exit) {
    JSR_CHECK(Int, 1, "n");
    exit(jsrGetNumber(vm, 1));
}

JSR_NATIVE(jsr_isPosix) {
#ifdef JSTAR_POSIX
    jsrPushBoolean(vm, true);
#else
    jsrPushBoolean(vm, false);
#endif
    return true;
}

JSR_NATIVE(jsr_platform) {
    jsrPushString(vm, PLATFORM);
    return true;
}

JSR_NATIVE(jsr_time) {
    jsrPushNumber(vm, time(NULL));
    return true;
}

JSR_NATIVE(jsr_clock) {
    jsrPushNumber(vm, (double)clock() / CLOCKS_PER_SEC);
    return true;
}

JSR_NATIVE(jsr_getenv) {
    JSR_CHECK(String, 1, "name");
    char* value = getenv(jsrGetString(vm, 1));
    if(value != NULL) {
        jsrPushString(vm, value);
        return true;
    }
    jsrPushNull(vm);
    return true;
}

JSR_NATIVE(jsr_exec) {
#ifdef USE_POPEN
    JSR_CHECK(String, 1, "cmd");

    FILE* proc = popen(jsrGetString(vm, 1), "r");
    if(proc == NULL) {
        JSR_RAISE(vm, "Exception", strerror(errno));
    }

    JStarBuffer data;
    jsrBufferInit(vm, &data);

    char buf[512];
    while(fgets(buf, 512, proc) != NULL) {
        jsrBufferAppendstr(&data, buf);
    }

    if(ferror(proc)) {
        pclose(proc);
        jsrBufferFree(&data);
        JSR_RAISE(vm, "Exception", strerror(errno));
    } else {
        jsrPushNumber(vm, pclose(proc));
        jsrBufferPush(&data);
        jsrPushTuple(vm, 2);
    }

    return true;
#else
    JSR_RAISE(vm, "NotImplementedException", "`exec` not supported on current system.");
#endif
}

JSR_NATIVE(jsr_system) {
    const char* cmd = NULL;
    if(!jsrIsNull(vm, 1)) {
        JSR_CHECK(String, 1, "cmd");
        cmd = jsrGetString(vm, 1);
    }
    jsrPushNumber(vm, system(cmd));
    return true;
}
