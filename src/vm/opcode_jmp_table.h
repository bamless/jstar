#include "opcode.h"

#define DEFINE_JMP_TABLE() \
	static void *opJmpTable[] = { \
		OPCODE(JMPTARGET) \
	}

#define JMPTARGET(X) &&TARGET_##X,

DEFINE_JMP_TABLE();
