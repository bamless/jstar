#include "disassemble.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "object.h"
#include "opcode.h"
#include "util.h"
#include "value.h"

// Create string names of opcodes
static const char* OpcodeNames[] = {
#define OPCODE(opcode, _) #opcode,
#include "opcode.def"
};

static uint16_t readShortAt(const uint8_t* code, size_t i) {
    return ((uint16_t)code[i] << 8) | code[i + 1];
}

void disassembleCode(Code* c) {
    for(size_t i = 0; i < c->count; i += opcodeArgsNumber(c->bytecode[i]) + 1) {
        disassembleIstr(c, i);
        if(c->bytecode[i] == OP_CLOSURE) {
            Value func = c->consts.arr[readShortAt(c->bytecode, i + 1)];
            i += (AS_FUNC(func)->upvalueCount + 1) * 2;
        }
    }
}

static void signedOffsetInstruction(Code* c, size_t i) {
    int16_t off = (int16_t)readShortAt(c->bytecode, i + 1);
    printf("%d (to %lu)", off, (unsigned long)(i + off + 3));
}

static void constInstruction(Code* c, size_t i) {
    int op = readShortAt(c->bytecode, i + 1);
    printf("%d (", op);
    printValue(c->consts.arr[op]);
    printf(")");
}

static void const2Instruction(Code* c, size_t i) {
    int arg1 = readShortAt(c->bytecode, i + 1);
    int arg2 = readShortAt(c->bytecode, i + 3);
    printf("%d %d (", arg1, arg2);
    printValue(c->consts.arr[arg1]);
    printf(", ");
    printValue(c->consts.arr[arg2]);
    printf(")");
}

static void invokeInstruction(Code* c, size_t i) {
    int argc = c->bytecode[i + 1];
    int name = readShortAt(c->bytecode, i + 2);
    printf("%d %d (", argc, name);
    printValue(c->consts.arr[name]);
    printf(")");
}

static void unsignedByteInstruction(Code* c, size_t i) {
    printf("%d", c->bytecode[i + 1]);
}

static void closureInstruction(Code* c, size_t i) {
    int op = readShortAt(c->bytecode, i + 1);

    printf("%d (", op);
    printValue(c->consts.arr[op]);
    printf(")");

    ObjFunction* fn = AS_FUNC(c->consts.arr[op]);

    int offset = i + 3;
    for(uint8_t j = 0; j < fn->upvalueCount; j++) {
        bool isLocal = c->bytecode[offset++];
        int index = c->bytecode[offset++];
        printf("\n%04d              | %s %d", offset - 2, isLocal ? "local" : "upvalue", index);
    }
}

void disassembleIstr(Code* c, size_t i) {
    printf("%.4d %s ", (int)i, OpcodeNames[c->bytecode[i]]);

    switch(c->bytecode[i]) {
    case OP_NATIVE:
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
    case OP_INVOKE_UNPACK:
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
    case OP_SUPER_BIND:
    case OP_SUPER_UNPACK:
    case OP_GET_CONST:
    case OP_GET_GLOBAL:
    case OP_SET_GLOBAL:
    case OP_DEFINE_GLOBAL:
        constInstruction(c, i);
        break;
    case OP_JUMP:
    case OP_JUMPT:
    case OP_JUMPF:
    case OP_FOR_NEXT:
    case OP_SETUP_EXCEPT:
    case OP_SETUP_ENSURE:
        signedOffsetInstruction(c, i);
        break;
    case OP_IMPORT_AS:
    case OP_NAT_METHOD:
    case OP_IMPORT_NAME:
        const2Instruction(c, i);
        break;
    case OP_INVOKE:
    case OP_SUPER:
        invokeInstruction(c, i);
        break;
    case OP_CALL:
    case OP_CALL_UNPACK:
    case OP_UNPACK:
    case OP_NEW_TUPLE:
    case OP_GET_LOCAL:
    case OP_SET_LOCAL:
    case OP_GET_UPVALUE:
    case OP_SET_UPVALUE:
        unsignedByteInstruction(c, i);
        break;
    case OP_CLOSURE:
        closureInstruction(c, i);
        break;
    default:
        break;
    }

    printf("\n");
}

int opcodeArgsNumber(Opcode op) {
    // clang-format off
    switch(op) {
    #define OPCODE(opcode, args) case opcode: return args;
    #include "opcode.def"
    }
    // clang-format on
    UNREACHABLE();
    return -1;
}
