#ifndef EXCS_H
#define EXCS_H

#include "jstar.h"

// Excepttion class fields
#define EXC_ERR   "_err"
#define EXC_CAUSE "_cause"
#define EXC_TRACE "_stacktrace"

// class Exception
JSR_NATIVE(jsr_Exception_printStacktrace);
JSR_NATIVE(jsr_Exception_getStacktrace);
// end

#endif
