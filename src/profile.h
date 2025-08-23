#ifndef PROFILE_H
#define PROFILE_H

#include "conf.h"

#ifdef JSTAR_INSTRUMENT

#include <stdint.h>

typedef struct InstrumentationTimer {
    const char* name;
    uint64_t start;
} InstrumentationTimer;

void startProfileSession(const char* filePath);
void endProfileSession(void);

InstrumentationTimer startProfileTimer(const char* name);
void endProfileTimer(const InstrumentationTimer* timer);

#endif

#ifdef JSTAR_INSTRUMENT
    #define PROFILE_LINE2_(name, line)     \
        InstrumentationTimer _timer_##line \
            __attribute__((__cleanup__(endProfileTimer))) = startProfileTimer(name);

    #define PROFILE_LINE_(name, line) PROFILE_LINE2_(name, line)

    #define PROFILE_BEGIN_SESSION(name) startProfileSession(name);
    #define PROFILE_END_SESSION()       endProfileSession();

    #define PROFILE(name)  PROFILE_LINE_(name, __LINE__)
    #define PROFILE_FUNC() PROFILE(__func__)
#else
    #define PROFILE_BEGIN_SESSION(name)
    #define PROFILE_END_SESSION()
    #define PROFILE_FUNC()
    #define PROFILE(name)
#endif

#endif  // PROFILER_H
