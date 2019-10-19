#include "sys.h"
#include "io.h"
#include "memory.h"
#include "vm.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

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

JSR_NATIVE(jsr_exec) {
    const char *cmd = NULL;
    if(!jsrIsNull(vm, 1)) {
        if(!jsrCheckStr(vm, 1, "cmd")) return false;
        cmd = jsrGetString(vm, 1);
    }
    jsrPushNumber(vm, system(cmd));
    return true;
}

JSR_NATIVE(jsr_exit) {
    if(!jsrCheckInt(vm, 1, "n")) return false;
    exit(jsrGetNumber(vm, 1));
}

JSR_NATIVE(jsr_getImportPaths) {
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

JSR_NATIVE(jsr_gc) {
    garbageCollect(vm);
    jsrPushNull(vm);
    return true;
}

JSR_NATIVE(jsr_gets) {
    JStarBuffer b;
    jsrBufferInit(vm, &b);

    int c;
    while((c = getc(stdin)) != EOF && c != '\n') {
        jsrBufferAppendChar(&b, c);
    }

    jsrBufferPush(&b);
    return true;
}

JSR_NATIVE(jsr_sys_init) {
    // Set up the standard I/O streams (this is a little bit of an hack)
    if(!jsrGetGlobal(vm, "io", "File")) return false;

    // Set stdout
    jsrDup(vm);
    jsrPushNull(vm);
    jsrPushNull(vm);
    jsrPushHandle(vm, stdout);
    if(jsrCall(vm, 3) != VM_EVAL_SUCCESS) return false;
    jsrSetGlobal(vm, NULL, "out");
    jsrPop(vm);

    // Set stderr
    jsrDup(vm);
    jsrPushNull(vm);
    jsrPushNull(vm);
    jsrPushHandle(vm, stderr);
    if(jsrCall(vm, 3) != VM_EVAL_SUCCESS) return false;
    jsrSetGlobal(vm, NULL, "err");
    jsrPop(vm);

    // Set stdin
    jsrPushNull(vm);
    jsrPushNull(vm);
    jsrPushHandle(vm, stdin);
    if(jsrCall(vm, 3) != VM_EVAL_SUCCESS) return false;
    jsrSetGlobal(vm, NULL, "stdin");
    jsrPop(vm);

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
