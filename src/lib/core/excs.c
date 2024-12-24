#include "excs.h"

#include <string.h>

#include "object.h"
#include "vm.h"

// class Exception
#define INDENT "    "

static bool recordEquals(FrameRecord* f1, FrameRecord* f2) {
    return f1 && f2 && (strcmp(f1->moduleName->data, f2->moduleName->data) == 0) &&
           (strcmp(f1->funcName->data, f2->funcName->data) == 0) && (f1->line == f2->line);
}

JSR_NATIVE(jsr_Exception_printStacktrace) {
    ObjInstance* exc = AS_INSTANCE(vm->apiStack[0]);
    ObjClass* cls = exc->base.cls;

    Value stacktraceVal = NULL_VAL;
    getField(vm, cls, exc, copyString(vm, EXC_TRACE, strlen(EXC_TRACE)), &stacktraceVal);

    if(IS_STACK_TRACE(stacktraceVal)) {
        Value cause = NULL_VAL;
        getField(vm, cls, exc, copyString(vm, EXC_CAUSE, strlen(EXC_CAUSE)), &cause);

        if(isInstance(vm, cause, vm->excClass)) {
            push(vm, cause);
            if(jsrCallMethod(vm, "printStacktrace", 0) != JSR_SUCCESS)
                return false;
            pop(vm);
            fprintf(stderr, "\nAbove Excetption caused:\n");
        }

        ObjStackTrace* stacktrace = AS_STACK_TRACE(stacktraceVal);

        if(stacktrace->recordSize > 0) {
            FrameRecord* lastRecord = NULL;

            fprintf(stderr, "Traceback (most recent call last):\n");
            for(int i = stacktrace->recordSize - 1; i >= 0; i--) {
                FrameRecord* record = &stacktrace->records[i];

                if(recordEquals(lastRecord, record)) {
                    int repetitions = 1;
                    while(i > 0) {
                        record = &stacktrace->records[i - 1];
                        if(!recordEquals(lastRecord, record)) break;
                        repetitions++, i--;
                    }
                    fprintf(stderr, INDENT "...\n");
                    fprintf(stderr, INDENT "[Previous line repeated %d times]\n", repetitions);
                    continue;
                }

                fprintf(stderr, INDENT);

                if(record->line >= 0) {
                    fprintf(stderr, "[line %d]", record->line);
                } else {
                    fprintf(stderr, "[line ?]");
                }
                fprintf(stderr, " module %s in %s\n", record->moduleName->data,
                        record->funcName->data);

                lastRecord = record;
            }
        }
    }

    Value err = NULL_VAL;
    getField(vm, cls, exc, copyString(vm, EXC_ERR, strlen(EXC_ERR)), &err);

    if(IS_STRING(err) && AS_STRING(err)->length > 0) {
        fprintf(stderr, "%s: %s\n", exc->base.cls->name->data, AS_STRING(err)->data);
    } else {
        fprintf(stderr, "%s\n", exc->base.cls->name->data);
    }

    jsrPushNull(vm);
    return true;
}

JSR_NATIVE(jsr_Exception_getStacktrace) {
    ObjInstance* exc = AS_INSTANCE(vm->apiStack[0]);

    JStarBuffer buf;
    jsrBufferInitCapacity(vm, &buf, 64);

    Value stval = NULL_VAL;
    getField(vm, exc->base.cls, exc, copyString(vm, EXC_TRACE, strlen(EXC_TRACE)), &stval);

    if(IS_STACK_TRACE(stval)) {
        Value cause = NULL_VAL;
        getField(vm, exc->base.cls, exc, copyString(vm, EXC_CAUSE, strlen(EXC_CAUSE)), &cause);

        if(isInstance(vm, cause, vm->excClass)) {
            push(vm, cause);
            if(jsrCallMethod(vm, "getStacktrace", 0) != JSR_SUCCESS)
                return false;
            Value stackTrace = peek(vm);
            if(IS_STRING(stackTrace)) {
                jsrBufferAppend(&buf, AS_STRING(stackTrace)->data, AS_STRING(stackTrace)->length);
                jsrBufferAppendStr(&buf, "\n\nAbove Exception caused:\n");
            }
            pop(vm);
        }

        ObjStackTrace* stacktrace = AS_STACK_TRACE(stval);

        if(stacktrace->recordSize > 0) {
            FrameRecord* lastRecord = NULL;

            jsrBufferAppendf(&buf, "Traceback (most recent call last):\n");
            for(int i = stacktrace->recordSize - 1; i >= 0; i--) {
                FrameRecord* record = &stacktrace->records[i];

                if(recordEquals(lastRecord, record)) {
                    int repetitions = 1;
                    while(i > 0) {
                        record = &stacktrace->records[i - 1];
                        if(!recordEquals(lastRecord, record)) break;
                        repetitions++, i--;
                    }
                    jsrBufferAppendStr(&buf, INDENT "...\n");
                    jsrBufferAppendf(&buf, INDENT "[Previous line repeated %d times]\n",
                                     repetitions);
                    continue;
                }

                jsrBufferAppendStr(&buf, "    ");

                if(record->line >= 0) {
                    jsrBufferAppendf(&buf, "[line %d]", record->line);
                } else {
                    jsrBufferAppendStr(&buf, "[line ?]");
                }

                jsrBufferAppendf(&buf, " module %s in %s\n", record->moduleName->data,
                                 record->funcName->data);

                lastRecord = record;
            }
        }
    }

    Value err = NULL_VAL;
    getField(vm, exc->base.cls, exc, copyString(vm, EXC_ERR, strlen(EXC_ERR)), &err);

    if(IS_STRING(err) && AS_STRING(err)->length > 0) {
        jsrBufferAppendf(&buf, "%s: %s", exc->base.cls->name->data, AS_STRING(err)->data);
    } else {
        jsrBufferAppendf(&buf, "%s", exc->base.cls->name->data);
    }

    jsrBufferPush(&buf);
    return true;
}
// end
