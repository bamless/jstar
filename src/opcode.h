#ifndef OPCODE_H
#define OPCODE_H

typedef enum Opcode {
	OP_HALT,

	//arithmetic operations
	OP_ADD,
	OP_SUB,
	OP_MULT,
	OP_DIV,
	OP_MOD,
	//equality operations
	OP_EQ,
	OP_NEQ,
	//logical operations
	OP_AND,
	OP_OR,
	OP_NOT,
	//comparison operations
	OP_GT,
	OP_GE,
	OP_LT,
	OP_LE,
	//call
	OP_CALL,

	OP_JUMP,
	OP_JUMPT,
	OP_JUMPF,

	//stack operations
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
