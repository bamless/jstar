#include "instrumentor.h"

#ifdef JSTAR_INSTRUMENT

#include <stdlib.h>
#include <stdio.h>
#include <time.h>

typedef struct IntrumentorResult {
    const char* name;
    long start, end;
} IntrumentorResult;

static FILE* sessionFile = NULL;
static int profileCount = 0;

static void writeHeader(void) {
    fputs("{\"otherData\": {},\"traceEvents\":[", sessionFile);
}

static void writeFooter(void) {
    fputs("]}", sessionFile);
}

void startInstrumentSession(const char* filePath) {
    sessionFile = fopen(filePath, "w");
    if(!sessionFile) {
        fprintf(stderr, "Cannot open session file\n");
        abort();
    }
    writeHeader();
}

void endInstrumentSession(void) {
    writeFooter();

    int res = fclose(sessionFile);
    if(res) {
        fprintf(stderr, "Cannot close session file\n");
        abort();
    }

    sessionFile = NULL;
    profileCount = 0;
}

static void writeInstrumentRecord(const char* name, long startNano, long endNano) {
    if(!sessionFile) {
        fprintf(stderr, "No session started\n");
        abort();
    }

    if(profileCount++ > 0) {
        fputc(',', sessionFile);
    }

    fputs("{\"cat\":\"function\",", sessionFile);
    fprintf(sessionFile, "\"dur\":%ld,", (endNano - startNano) / 1000);
    fprintf(sessionFile, "\"name\":\"%s\",", name);
    fputs("\"ph\":\"X\",", sessionFile);
    fputs("\"pid\":0,", sessionFile);
    fputs("\"tid\":0,", sessionFile);
    fprintf(sessionFile, "\"ts\":%lf", (double)(startNano) / 1000.0);
    fputc('}', sessionFile);
}

static long getTime(void) {
    struct timespec time;
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &time);
    return time.tv_nsec;
}

InstrumentationTimer startTimer(const char* name) {
    return (InstrumentationTimer){.name = name, .startTime = getTime()};
}

void endTimer(const InstrumentationTimer* timer) {
    writeInstrumentRecord(timer->name, timer->startTime, getTime());
}

#endif