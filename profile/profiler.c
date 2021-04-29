#include "profiler.h"

#ifdef JSTAR_INSTRUMENT

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static FILE* sessionFile = NULL;
static int profileCount = 0;

static void writeHeader(void) {
    fputs("{\"otherData\": {},\"traceEvents\":[", sessionFile);
    fflush(sessionFile);
}

static void writeFooter(void) {
    fputs("]}", sessionFile);
    fflush(sessionFile);
}

void startProfileSession(const char* filePath) {
    sessionFile = fopen(filePath, "w");
    if(!sessionFile) {
        fprintf(stderr, "Cannot open session file\n");
        abort();
    }
    writeHeader();
}

void endProfileSession(void) {
    writeFooter();

    int res = fclose(sessionFile);
    if(res) {
        fprintf(stderr, "Cannot close session file\n");
        abort();
    }

    sessionFile = NULL;
    profileCount = 0;
}

static void writeInstrumentRecord(const char* name, uint64_t startNano, uint64_t endNano) {
    if(!sessionFile) {
        fprintf(stderr, "No session started\n");
        abort();
    }

    double timestamp = startNano / 1000.0;
    double elapsed = (endNano - startNano) / 1000.0;

    if(profileCount++ > 0) {
        fputc(',', sessionFile);
    }
    fprintf(sessionFile,
            "{\"cat\":\"function\",\"dur\":%g,\"name\":\"%s\",\"ph\":\"X\",\"pid\":0,\"tid\":0,"
            "\"ts\":%g}",
            elapsed, name, timestamp);
}

InstrumentationTimer startProfileTimer(const char* name) {
    struct timespec tp;
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &tp);
    return (InstrumentationTimer){.name = name, .start = tp.tv_sec * 1000000000LL + tp.tv_nsec};
}

void endProfileTimer(const InstrumentationTimer* timer) {
    struct timespec tp;
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &tp);
    writeInstrumentRecord(timer->name, timer->start, tp.tv_sec * 1000000000LL + tp.tv_nsec);
}

#endif