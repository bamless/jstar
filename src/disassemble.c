#include "disassemble.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "opcode.h"
#include "util.h"
#include "value.h"

#define INDENT 4

static uint16_t readShortAt(const uint8_t* code, size_t i) {
    return ((uint16_t)code[i] << 8) | code[i + 1];
}

static size_t countInstructions(Code* c) {
    size_t count = 0;
    for(size_t i = 0; i < c->count;) {
        count++;
        Opcode instr = c->bytecode[i];
        if(instr == OP_CLOSURE) {
            Value func = c->consts.arr[readShortAt(c->bytecode, i + 1)];
            i += AS_FUNC(func)->upvalueCount * 2;
        }
        i += opcodeArgsNumber(instr) + 1;
    }
    return count;
}

static void disassembleCode(Code* c, int indent) {
    for(size_t i = 0; i < c->count;) {
        disassembleIstr(c, indent, i);
        Opcode instr = c->bytecode[i];
        if(instr == OP_CLOSURE) {
            Value func = c->consts.arr[readShortAt(c->bytecode, i + 1)];
            i += AS_FUNC(func)->upvalueCount * 2;
        }
        i += opcodeArgsNumber(instr) + 1;
    }
}

static void disassembleCommon(FnCommon* c, int upvals) {
    printf("arguments %d, defaults %d, upvalues %d", (int)c->argsCount, (int)c->defCount, upvals);
    if(c->vararg) printf(", vararg");
    printf("\n");
}

void disassembleFunction(ObjFunction* fn) {
    ObjString* mod = fn->c.module->name;
    ObjString* name = fn->c.name;
    size_t instr = countInstructions(&fn->code);

    printf("function ");
    if(mod->length != 0) {
        printf("%s.%s", mod->data, name->data);
    } else {
        printf("%s", name->data);
    }
    printf(" (%zu instructions at %p)\n", instr, (void*)fn);
    
    disassembleCommon(&fn->c, fn->upvalueCount);
    disassembleCode(&fn->code, INDENT);

    for(int i = 0; i < fn->code.consts.count; i++) {
        Value c = fn->code.consts.arr[i];
        if(IS_FUNC(c)) {
            printf("\n");
            disassembleFunction(AS_FUNC(c));
        } else if(IS_NATIVE(c)) {
            printf("\n");
            disassembleNative(AS_NATIVE(c));
        }
    }
}

void disassembleNative(ObjNative* nat) {
    ObjString* mod = nat->c.module->name;
    ObjString* name = nat->c.name;
    printf("native ");
    if(mod->length != 0) {
        printf("%s.%s", mod->data, name->data);
    } else {
        printf("%s", name->data);
    }
    printf(" (%p)\n", (void*)nat);
    disassembleCommon(&nat->c, 0);
}

static void signedOffsetInstruction(Code* c, size_t i) {
    int16_t off = (int16_t)readShortAt(c->bytecode, i + 1);
    printf("%d (to %zu)", off, (size_t)(i + off + 3));
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

static void closureInstruction(Code* c, int indent, size_t i) {
    int op = readShortAt(c->bytecode, i + 1);

    printf("%d (", op);
    printValue(c->consts.arr[op]);
    printf(")");

    ObjFunction* fn = AS_FUNC(c->consts.arr[op]);

    size_t offset = i + 3;
    for(uint8_t j = 0; j < fn->upvalueCount; j++) {
        bool isLocal = c->bytecode[offset++];
        int index = c->bytecode[offset++];

        printf("\n");
        for(int i = 0; i < indent; i++) {
            printf(" ");
        }
        printf("%04zu              | %s %d", offset - 2, isLocal ? "local" : "upvalue", index);
    }
}

void disassembleIstr(Code* c, int indent, size_t i) {
    for(int i = 0; i < indent; i++) {
        printf(" ");
    }
    printf("%.4zu %s ", i, OpcodeNames[c->bytecode[i]]);

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
    case OP_POPN:
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
        closureInstruction(c, indent, i);
        break;
    default:
        break;
    }

    printf("\n");
}