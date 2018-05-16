#include "opcode.h"

#ifdef DBG_PRINT_EXEC
const char *opName[] = {
	//arithmetic operations
	"OP_ADD",
	"OP_SUB",
	"OP_MUL",
	"OP_DIV",
	"OP_MOD",
	"OP_NEG",
	//equality operations
	"OP_EQ",
	"OP_NEQ",
	//logical operations
	"OP_NOT",
	//comparison operations
	"OP_GT",
	"OP_GE",
	"OP_LT",
	"OP_LE",
	"OP_IS",
	//access
	"OP_GET_FIELD",
	"OP_SET_FIELD",
	//call
	"OP_CALL",
	"OP_CALL_0",
	"OP_CALL_1",
	"OP_CALL_2",
	"OP_CALL_3",
	"OP_CALL_4",
	"OP_CALL_5",
	"OP_CALL_6",
	"OP_CALL_7",
	"OP_CALL_8",
	"OP_CALL_9",
	"OP_CALL_10",
	//method
	"OP_INVOKE",
	"OP_INVOKE_0",
	"OP_INVOKE_1",
	"OP_INVOKE_2",
	"OP_INVOKE_3",
	"OP_INVOKE_4",
	"OP_INVOKE_5",
	"OP_INVOKE_6",
	"OP_INVOKE_7",
	"OP_INVOKE_8",
	"OP_INVOKE_9",
	"OP_INVOKE_10",

	"OP_SUPER",
	"OP_SUPER_0",
	"OP_SUPER_1",
	"OP_SUPER_2",
	"OP_SUPER_3",
	"OP_SUPER_4",
	"OP_SUPER_5",
	"OP_SUPER_6",
	"OP_SUPER_7",
	"OP_SUPER_8",
	"OP_SUPER_9",
	"OP_SUPER_10",

	"OP_JUMP",
	"OP_JUMPT",
	"OP_JUMPF",

	"OP_PRINT",
	"OP_IMPORT",
	"OP_IMPORT_AS",

	//stack operations
	"OP_NEW_CLASS",
	"OP_NEW_SUBCLASS",
	"OP_DEF_METHOD",
	"OP_NAT_METHOD",
	"OP_GET_CONST",
	"OP_GET_LOCAL",
	"OP_GET_GLOBAL",
	"OP_SET_LOCAL",
	"OP_SET_GLOBAL",
	"OP_DEFINE_GLOBAL",
	"OP_DEFINE_NATIVE",
	"OP_RETURN",
	"OP_NULL",
	"OP_POP",
	"OP_DUP",
};
#endif
