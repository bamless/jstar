#ifndef SYS_H
#define SYS_H

#include "jstar.h"

JSR_NATIVE(jsr_time);
JSR_NATIVE(jsr_exit);
JSR_NATIVE(jsr_exec);
JSR_NATIVE(jsr_isPosix);
JSR_NATIVE(jsr_platform);
JSR_NATIVE(jsr_getenv);
JSR_NATIVE(jsr_clock);
JSR_NATIVE(jsr_system);
JSR_NATIVE(jsr_eval);

#endif
