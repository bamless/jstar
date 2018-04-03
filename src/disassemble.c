#include "disassemble.h"
#include "opcode.h"

#include <stdio.h>

void disassemble(Chunk *c) {
	size_t i = 0;
	while(i < c->count) {
		printf("%s ", opName[c->code[i]]);
		switch(c->code[i]) {
		case OP_JUMP:
		case OP_JUMPT:
		case OP_JUMPF:
			printf("%d ", (int16_t)((uint16_t)c->code[i + 1] << 8) | c->code[i + 2]);
			i += 2;
			break;

		case OP_CALL:
			printf("%d ", c->code[i + 1]);
			i++;
			break;
		//stack operations
		case OP_GET_CONST:
		case OP_GET_LOCAL:
		case OP_GET_GLOBAL:
		case OP_SET_LOCAL:
		case OP_SET_GLOBAL:
		case OP_DEFINE_GLOBAL:
			printf("%d ", c->code[i + 1]);
			i++;
			break;
		default: break;
		}
		i++;
		printf("\n");
	}
}
