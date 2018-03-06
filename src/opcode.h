#ifndef OPCODE_H
#define OPCODE_H

typedef enum Opcode {
	OP_HALT,

	//arithmetic operations
	OP_ADD,
	op_SUB,
	OP_MULT,
	OP_DIV,
	OP_MOD,

	//stack operations
	OP_GET_LOCAL,
	OP_GET_GLOBAL,
	OP_SET_LOCAL,
	OP_SET_GLOBAL,
	OP_DEFINE_GLOBAL,
	OP_POP,
	OP_DUP,
} Opcode;

#endif
