#include "disassemble.h"
#include "opcode.h"
#include "object.h"

#include <stdio.h>

void disassembleChunk(Chunk *c) {
	for(size_t i = 0; i < c->count; i += opcodeArgsNumber(c->code[i]) + 1) {
		int extraArgs = 0;
		if(c->code[i] == OP_NEW_CLOSURE) {
			Value v = c->consts.arr[((uint16_t)c->code[i + 1] << 8) | c->code[i + 2]];
			extraArgs = AS_FUNC(v)->upvaluec * 2 + 1;
		}
			
		disassembleIstr(c, i);
		i += extraArgs;
	}
}

void disassembleIstr(Chunk *c, size_t i) {
	printf("%.4d %s ", (int) i, OpcodeName[c->code[i]]);

	// decode arguments
	switch(c->code[i]) {
	// instructions with 2 arguments representing signed offset
	case OP_JUMP:
	case OP_JUMPT:
	case OP_JUMPF:
	case OP_SETUP_EXCEPT:
	case OP_SETUP_ENSURE: {
		int16_t off = (int16_t)((uint16_t)c->code[i + 1] << 8) | c->code[i + 2];
		printf("%d (to %lu)", off, (unsigned long)(i + off + 3));
		break;
	}

	// instructions with 2 arguments representing 2 constant values
	case OP_IMPORT_AS:
	case OP_NAT_METHOD:
	case OP_IMPORT_NAME: {
		int arg1 = ((uint16_t)c->code[i + 1] << 8) | c->code[i + 2];
		int arg2 = ((uint16_t)c->code[i + 3] << 8) | c->code[i + 4];

		printf("%d %d (", arg1, arg2);
		printValue(c->consts.arr[arg1]);
		printf(", ");
		printValue(c->consts.arr[arg2]);
		printf(")");
		break;
	}

	// method call instructions, 2 arguments representing argc and method name
	case OP_INVOKE:
	case OP_SUPER: {
		int argc = c->code[i + 1];
		int name = ((uint16_t)c->code[i + 2] << 8) | c->code[i + 3];
		printf("%d %d (", argc, name);
		printValue(c->consts.arr[name]);
		printf(")");
		break;
	}

	// instructions with 1 argument representing constant value
	case OP_IMPORT:
	case OP_IMPORT_FROM:
	case OP_GET_FIELD:
	case OP_SET_FIELD:
	case OP_NEW_CLASS:
	case OP_NEW_SUBCLASS:
	case OP_DEF_METHOD:
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
	case OP_GET_GLOBAL:
	case OP_SET_GLOBAL:
	case OP_DEFINE_NATIVE:
	case OP_DEFINE_GLOBAL: {
		int op = ((uint16_t)c->code[i + 1] << 8) | c->code[i + 2];
		printf("%d (", op);
		printValue(c->consts.arr[op]);
		printf(")");
		break;
	}

	// instructions with 1 argument representing an unsigned 1 byte integer
	case OP_CALL:
	case OP_GET_LOCAL:
	case OP_SET_LOCAL:
	case OP_GET_UPVALUE:
	case OP_SET_UPVALUE:
		printf("%d", c->code[i + 1]);
		break;

	case OP_NEW_CLOSURE: {
		int op = ((uint16_t)c->code[i + 1] << 8) | c->code[i + 2];

		printf("%d (", op);
		printValue(c->consts.arr[op]);
		printf(")\n");

		ObjFunction *fn = AS_FUNC(c->consts.arr[op]);

		int offset = i + 3;
		for(uint8_t j = 0; j < fn->upvaluec; j++) {
			bool isLocal = c->code[offset++];
			int index = c->code[offset++];
			printf("%04d              | %s %d\n",  
				offset - 2, isLocal ? "local" : "upvalue", index);
		}

		break;
	}
	}

	printf("\n");
}
