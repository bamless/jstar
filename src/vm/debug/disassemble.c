#include "disassemble.h"
#include "opcode.h"

#include <stdio.h>

#ifdef DBG_PRINT_EXEC
void disassembleIstr(Chunk *c, size_t i) {
	int n = getBytecodeSrcLine(c, i);

	printf("%.4d %s ", n, OpcodeName[c->code[i]]);
	switch(c->code[i]) {
	case OP_JUMP:
	case OP_JUMPT:
	case OP_JUMPF:
	case OP_SETUP_TRY:
		printf("%d", (int16_t)((uint16_t)c->code[i + 1] << 8) | c->code[i + 2]);
		break;

	case OP_IMPORT_AS:
	case OP_INVOKE:
	case OP_SUPER:
	case OP_NAT_METHOD:
	case OP_DEF_METHOD:
		printf("%d %d", c->code[i + 1], c->code[i + 2]);
		break;

	case OP_IMPORT:
	case OP_GET_FIELD:
	case OP_SET_FIELD:
	case OP_NEW_CLASS:
	case OP_NEW_SUBCLASS:
	case OP_CALL:
	//stack operations
	case OP_INVOKE_0:
	case OP_INVOKE_1:
	case OP_INVOKE_2:
	case OP_INVOKE_3:
	case OP_INVOKE_4:
	case OP_INVOKE_5:
	case OP_INVOKE_6:
	case OP_INVOKE_7:
	case OP_INVOKE_8:
	case OP_INVOKE_9:
	case OP_INVOKE_10:
	case OP_SUPER_0:
	case OP_SUPER_1:
	case OP_SUPER_2:
	case OP_SUPER_3:
	case OP_SUPER_4:
	case OP_SUPER_5:
	case OP_SUPER_6:
	case OP_SUPER_7:
	case OP_SUPER_8:
	case OP_SUPER_9:
	case OP_SUPER_10:
	case OP_GET_CONST:
	case OP_GET_LOCAL:
	case OP_GET_GLOBAL:
	case OP_SET_LOCAL:
	case OP_SET_GLOBAL:
	case OP_DEFINE_NATIVE:
	case OP_DEFINE_GLOBAL:
		printf("%d", c->code[i + 1]);
		break;
	default: break;
	}
	printf("\n");
}
#endif
