#ifndef OS_H
#define OS_H

#include "jstar.h"

JSR_NATIVE(jsr_time);
JSR_NATIVE(jsr_exec);
JSR_NATIVE(jsr_exit);
JSR_NATIVE(jsr_getImportPaths);
JSR_NATIVE(jsr_platform);
JSR_NATIVE(jsr_gc);
JSR_NATIVE(jsr_clock);
JSR_NATIVE(jsr_gets);

JSR_NATIVE(jsr_sys_init);

#endif
