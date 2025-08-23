#include "profile.h"

#ifdef JSTAR_INSTRUMENT

    #include <stdio.h>
    #include <stdlib.h>
    #include <time.h>

typedef struct ProfileSession {
    FILE* sessionFile;
    int profileCount;
    struct ProfileSession* prev;
} ProfileSession;

ProfileSession* session;

static void writeHeader(void) {
    fputs("{\"otherData\": {},\"traceEvents\":[", session->sessionFile);
    fflush(session->sessionFile);
}

static void writeFooter(void) {
    fputs("]}", session->sessionFile);
    fflush(session->sessionFile);
}

void startProfileSession(const char* filePath) {
    FILE* sessionFile = fopen(filePath, "w");
    if(!sessionFile) {
        fprintf(stderr, "Cannot open session file\n");
        abort();
    }

    ProfileSession* new_session = calloc(1, sizeof(*new_session));
    new_session->sessionFile = sessionFile;
    new_session->prev = session;
    session = new_session;

    writeHeader();
}

void endProfileSession(void) {
    writeFooter();

    int res = fclose(session->sessionFile);
    if(res) {
        fprintf(stderr, "Cannot close session file\n");
        abort();
    }

    ProfileSession* old = session;
    session = session->prev;
    free(old);
}

static void writeInstrumentRecord(const char* name, uint64_t startNano, uint64_t endNano) {
    if(!session) {
        fprintf(stderr, "No session started\n");
        abort();
    }

    double timestamp = startNano / 1000.0;
    double elapsed = (endNano - startNano) / 1000.0;

    if(session->profileCount++ > 0) {
        fputc(',', session->sessionFile);
    }
    fprintf(session->sessionFile,
            "{\"cat\":\"function\",\"dur\":%lf,\"name\":\"%s\",\"ph\":\"X\",\"pid\":0,\"tid\":0,"
            "\"ts\":%lf}",
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
