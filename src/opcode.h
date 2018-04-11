#ifndef OPCODE_H
#define OPCODE_H

extern const char *opName[];

typedef enum Opcode {
	//arithmetic operations
	OP_ADD,
	OP_SUB,
	OP_MUL,
	OP_DIV,
	OP_MOD,
	OP_NEG,
	//equality operations
	OP_EQ,
	OP_NEQ,
	//logical operations
	OP_NOT,
	//comparison operations
	OP_GT,
	OP_GE,
	OP_LT,
	OP_LE,
	//call
	OP_CALL,
	OP_CALL_0,
	OP_CALL_1,
	OP_CALL_2,
	OP_CALL_3,
	OP_CALL_4,
	OP_CALL_5,
	OP_CALL_6,
	OP_CALL_7,
	OP_CALL_8,
	OP_CALL_9,
	OP_CALL_10,

	OP_JUMP,
	OP_JUMPT,
	OP_JUMPF,

	OP_PRINT,

	//stack operations
	OP_NEW_CLASS,
	OP_NEW_SUBCLASS,
	OP_DEF_METHOD,
	OP_GET_CONST,
	OP_GET_LOCAL,
	OP_GET_GLOBAL,
	OP_SET_LOCAL,
	OP_SET_GLOBAL,
	OP_DEFINE_GLOBAL,
	OP_RETURN,
	OP_NULL,
	OP_POP,
	OP_DUP,
} Opcode;

#endif
