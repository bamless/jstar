#ifndef PROFILER_H
#define PROFILER_H

#include "profileconf.h"

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
    #define PROFILE_BEGIN_SESSION(name) startProfileSession(name);
    #define PROFILE_END_SESSION()       endProfileSession();

    #define CONCAT_(x, y)   x##y
    #define UNIQ_VAR_(name) CONCAT_(name, __LINE__)

    #define PROFILE_FUNC() PROFILE(__func__)

    #define PROFILE(name)                   \
        InstrumentationTimer UNIQ_VAR_(_t_) \
            __attribute__((__cleanup__(endProfileTimer))) = startProfileTimer(name);
#else
    #define PROFILE_BEGIN_SESSION(name)
    #define PROFILE_END_SESSION()
    #define PROFILE_FUNC()
    #define PROFILE(name)
#endif

#endif  // PROFILER_H
