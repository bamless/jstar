#include "disassemble.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "object.h"
#include "opcode.h"
#include "value.h"

#define INDENT 4

static uint16_t readShortAt(const uint8_t* code, size_t i) {
    return ((uint16_t)code[i] << 8) | code[i + 1];
}

static size_t countInstructions(const Code* c) {
    size_t count = 0;
    for(size_t i = 0; i < c->bytecode.count;) {
        count++;
        Opcode instr = c->bytecode.items[i];
        if(instr == OP_CLOSURE) {
            Value func = c->consts.items[readShortAt(c->bytecode.items, i + 1)];
            i += AS_FUNC(func)->upvalueCount * 2;
        }
        i += opcodeArgsNumber(instr) + 1;
    }
    return count;
}

static void disassembleCode(const Code* c, int indent) {
    for(size_t i = 0; i < c->bytecode.count;) {
        disassembleInstr(c, indent, i);
        Opcode instr = c->bytecode.items[i];
        if(instr == OP_CLOSURE) {
            Value func = c->consts.items[readShortAt(c->bytecode.items, i + 1)];
            i += AS_FUNC(func)->upvalueCount * 2;
        }
        i += opcodeArgsNumber(instr) + 1;
    }
}

static void disassemblePrototype(const Prototype* proto, int upvals) {
    printf("arguments %d, defaults %d, upvalues %d", (int)proto->argsCount, (int)proto->defCount,
           upvals);
    if(proto->vararg) printf(", vararg");
    printf("\n");
}

void disassembleFunction(const ObjFunction* fn) {
    ObjString* mod = fn->proto.module->name;
    ObjString* name = fn->proto.name;
    size_t instr = countInstructions(&fn->code);

    printf("function ");
    if(mod->length != 0) {
        printf("%s.%s", mod->data, name->data);
    } else {
        printf("%s", name->data);
    }
    printf(" (%zu instructions at %p)\n", instr, (void*)fn);

    disassemblePrototype(&fn->proto, fn->upvalueCount);
    disassembleCode(&fn->code, INDENT);

    for(size_t i = 0; i < fn->code.consts.count; i++) {
        Value c = fn->code.consts.items[i];
        if(IS_FUNC(c)) {
            printf("\n");
            disassembleFunction(AS_FUNC(c));
        } else if(IS_NATIVE(c)) {
            printf("\n");
            disassembleNative(AS_NATIVE(c));
        }
    }
}

void disassembleNative(const ObjNative* nat) {
    ObjString* mod = nat->proto.module->name;
    ObjString* name = nat->proto.name;
    printf("native ");
    if(mod->length != 0) {
        printf("%s.%s", mod->data, name->data);
    } else {
        printf("%s", name->data);
    }
    printf(" (%p)\n", (void*)nat);
    disassemblePrototype(&nat->proto, 0);
}

static void signedOffsetInstruction(const Code* c, size_t i) {
    int16_t off = (int16_t)readShortAt(c->bytecode.items, i + 1);
    printf("%d (to %zu)", off, (size_t)(i + off + 3));
}

static void constInstruction(const Code* c, size_t i) {
    int arg = readShortAt(c->bytecode.items, i + 1);
    printf("%d (", arg);
    printValue(c->consts.items[arg]);
    printf(")");
}

static void symbolInstruction(const Code* c, size_t i) {
    int arg = readShortAt(c->bytecode.items, i + 1);
    printf("%d (", arg);
    printValue(c->consts.items[c->symbols.items[arg].constant]);
    printf(")");
}

static void const2Instruction(const Code* c, size_t i) {
    int arg1 = readShortAt(c->bytecode.items, i + 1);
    int arg2 = readShortAt(c->bytecode.items, i + 3);
    printf("%d %d (", arg1, arg2);
    printValue(c->consts.items[arg1]);
    printf(", ");
    printValue(c->consts.items[arg2]);
    printf(")");
}

static void invokeInstruction(const Code* c, size_t i) {
    int argc = c->bytecode.items[i + 1];
    int name = readShortAt(c->bytecode.items, i + 2);
    printf("%d %d (", argc, name);
    printValue(c->consts.items[c->symbols.items[name].constant]);
    printf(")");
}

static void unsignedByteInstruction(const Code* c, size_t i) {
    printf("%d", c->bytecode.items[i + 1]);
}

static void closureInstruction(const Code* c, int indent, size_t i) {
    int op = readShortAt(c->bytecode.items, i + 1);

    printf("%d (", op);
    printValue(c->consts.items[op]);
    printf(")");

    ObjFunction* fn = AS_FUNC(c->consts.items[op]);

    size_t offset = i + 3;
    for(uint8_t j = 0; j < fn->upvalueCount; j++) {
        bool isLocal = c->bytecode.items[offset++];
        int index = c->bytecode.items[offset++];

        printf("\n");
        for(int i = 0; i < indent; i++) {
            printf(" ");
        }
        printf("%04zu              | %s %d", offset - 2, isLocal ? "local" : "upvalue", index);
    }
}

void disassembleInstr(const Code* c, int indent, size_t instr) {
    for(int i = 0; i < indent; i++) {
        printf(" ");
    }
    printf("%.4zu %s ", instr, OpcodeNames[c->bytecode.items[instr]]);

    switch((Opcode)c->bytecode.items[instr]) {
    case OP_IMPORT:
    case OP_IMPORT_FROM:
    case OP_NEW_CLASS:
    case OP_DEF_METHOD:
    case OP_GET_CONST:
        constInstruction(c, instr);
        break;
    case OP_GET_FIELD:
    case OP_SET_FIELD:
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
    case OP_SUPER_UNPACK:
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
    case OP_GET_GLOBAL:
    case OP_SET_GLOBAL:
    case OP_DEFINE_GLOBAL:
        symbolInstruction(c, instr);
        break;
    case OP_IMPORT_NAME:
    case OP_NATIVE:
    case OP_NATIVE_METHOD:
        const2Instruction(c, instr);
        break;
    case OP_JUMP:
    case OP_JUMPT:
    case OP_JUMPF:
    case OP_FOR_NEXT:
    case OP_SETUP_EXCEPT:
    case OP_SETUP_ENSURE:
        signedOffsetInstruction(c, instr);
        break;
    case OP_INVOKE:
    case OP_SUPER:
        invokeInstruction(c, instr);
        break;
    case OP_POPN:
    case OP_CALL:
    case OP_NEW_TUPLE:
    case OP_GET_LOCAL:
    case OP_SET_LOCAL:
    case OP_GET_UPVALUE:
    case OP_SET_UPVALUE:
        unsignedByteInstruction(c, instr);
        break;
    case OP_CLOSURE:
        closureInstruction(c, indent, instr);
        break;
    case OP_ADD:
    case OP_SUB:
    case OP_MUL:
    case OP_DIV:
    case OP_MOD:
    case OP_NEG:
    case OP_INVERT:
    case OP_BAND:
    case OP_BOR:
    case OP_XOR:
    case OP_LSHIFT:
    case OP_RSHIFT:
    case OP_EQ:
    case OP_NOT:
    case OP_GT:
    case OP_GE:
    case OP_LT:
    case OP_LE:
    case OP_IS:
    case OP_POW:
    case OP_SUBSCR_SET:
    case OP_SUBSCR_GET:
    case OP_CALL_0:
    case OP_CALL_1:
    case OP_CALL_2:
    case OP_CALL_3:
    case OP_CALL_4:
    case OP_CALL_5:
    case OP_CALL_6:
    case OP_CALL_7:
    case OP_CALL_8:
    case OP_CALL_9:
    case OP_CALL_10:
    case OP_CALL_UNPACK:
    case OP_FOR_PREP:
    case OP_FOR_ITER:
    case OP_NEW_LIST:
    case OP_APPEND_LIST:
    case OP_LIST_TO_TUPLE:
    case OP_NEW_TABLE:
    case OP_GENERATOR:
    case OP_GENERATOR_CLOSE:
    case OP_GET_OBJECT:
    case OP_SUBCLASS:
    case OP_RETURN:
    case OP_YIELD:
    case OP_NULL:
    case OP_END_HANDLER:
    case OP_POP_HANDLER:
    case OP_RAISE:
    case OP_POP:
    case OP_CLOSE_UPVALUE:
    case OP_DUP:
    case OP_UNPACK:
    case OP_END:
        // Nothing to do for no-arg instructions
        break;
    }

    printf("\n");
}
