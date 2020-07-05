#include "sys.h"

#include <stdbool.h>
#include <stdlib.h>
#include <time.h>

#include "memory.h"
#include "value.h"
#include "vm.h"

#ifdef _WIN32
    #define PLATFORM "win32"
#elif defined(__linux__)
    #define PLATFORM "linux"
#elif defined(__APPLE__)
    #define PLATFORM "darwin"
#elif defined(__FreeBSD__)
    #define PLATFORM "freebsd"
#else
    #define PLATFORM "unknown"
#endif

JSR_NATIVE(jsr_exit) {
    JSR_CHECK(Int, 1, "n");
    exit(jsrGetNumber(vm, 1));
}

JSR_NATIVE(jsr_importPaths) {
    push(vm, OBJ_VAL(vm->importpaths));
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

JSR_NATIVE(jsr_gc) {
    garbageCollect(vm);
    jsrPushNull(vm);
    return true;
}

JSR_NATIVE(jsr_sys_init) {
    // Set up command line arguments
    if(vm->argc != 0) {
        jsrGetGlobal(vm, NULL, "args");
        for(int i = 0; i < vm->argc; i++) {
            jsrPushString(vm, vm->argv[i]);
            jsrListAppend(vm, -2);
            jsrPop(vm);
        }
    }
    jsrPushNull(vm);
    return true;
}
