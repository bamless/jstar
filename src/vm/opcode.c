#include "opcode.h"

DEFINE_TO_STRING(Opcode, OPCODE);

int opcodeArgsNumber(Opcode op) {
	switch(op) {
	case OP_JUMP:
	case OP_JUMPT:
	case OP_JUMPF:
	case OP_SETUP_EXCEPT:
	case OP_SETUP_ENSURE:
		return 2;

	case OP_IMPORT_AS:
	case OP_IMPORT_NAME:
	case OP_NAT_METHOD:
		return 4;

	case OP_INVOKE:
	case OP_SUPER:
		return 3;

	case OP_IMPORT:
	case OP_DEF_METHOD:
	case OP_IMPORT_FROM:
	case OP_GET_FIELD:
	case OP_SET_FIELD:
	case OP_NEW_CLASS:
	case OP_NEW_SUBCLASS:
	case OP_NEW_CLOSURE:
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
	case OP_DEFINE_GLOBAL:
		return 2;
	
	case OP_CALL:
	case OP_UNPACK:
	case OP_NEW_TUPLE:
	case OP_GET_LOCAL:
	case OP_SET_LOCAL:
	case OP_GET_UPVALUE:
	case OP_SET_UPVALUE:
		return 1;
	default:
		return 0;
	}
}
