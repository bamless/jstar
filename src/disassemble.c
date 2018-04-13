#include "disassemble.h"
#include "opcode.h"

#include <stdio.h>

void disassembleIstr(Chunk *c, size_t i) {
	int n = getBytecodeSrcLine(c, i);

	printf("%.4d %s ", n, opName[c->code[i]]);
	switch(c->code[i]) {
	case OP_JUMP:
	case OP_JUMPT:
	case OP_JUMPF:
		printf("%d", (int16_t)((uint16_t)c->code[i + 1] << 8) | c->code[i + 2]);
		break;

	case OP_DEF_METHOD:
		printf("%d %d", c->code[i + 1], c->code[i + 2]);
		break;

	case OP_NEW_CLASS:
	case OP_NEW_SUBCLASS:
	case OP_CALL:
	//stack operations
	case OP_GET_CONST:
	case OP_GET_LOCAL:
	case OP_GET_GLOBAL:
	case OP_SET_LOCAL:
	case OP_SET_GLOBAL:
	case OP_DEFINE_GLOBAL:
		printf("%d", c->code[i + 1]);
		break;
	default: break;
	}
	printf("\n");
}
