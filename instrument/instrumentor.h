#ifndef INSTRUMENTOR_H
#define INSTRUMENTOR_H

#include "../include/jstar/jstarconf.h"

#ifdef JSTAR_INSTRUMENT

typedef struct InstrumentationTimer {
    const char* name;
    long startTime;
} InstrumentationTimer;

void startInstrumentSession(const char* filePath);
void endInstrumentSession(void);

InstrumentationTimer startTimer(const char* name);
void endTimer(const InstrumentationTimer* timer);

#endif

#ifdef JSTAR_INSTRUMENT
    #define PROFILE_BEGIN_SESSION(name) startInstrumentSession(name);
    #define PROFILE_END_SESSION()       endInstrumentSession();

    #define CONCAT_(x, y)   x##y
    #define UNIQ_VAR_(name) CONCAT_(name, __LINE__)

    #define PROFILE_FUNC() PROFILE(__func__)

    #define PROFILE(name)                  \
        InstrumentationTimer UNIQ_VAR_(_t_) \
            __attribute__((__cleanup__(endTimer))) = startTimer(name);
#else
    #define PROFILE_BEGIN_SESSION(name)
    #define PROFILE_END_SESSION()
    #define PROFILE_FUNC()
    #define PROFILE(name)
#endif

#endif // INSTRUMENTOR_H
