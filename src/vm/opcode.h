#ifndef OPCODE_H
#define OPCODE_H

#include "util.h"

#define OPCODE(X)       \
    X(OP_ADD)           \
    X(OP_SUB)           \
    X(OP_MUL)           \
    X(OP_DIV)           \
    X(OP_MOD)           \
    X(OP_NEG)           \
    X(OP_EQ)            \
    X(OP_NOT)           \
    X(OP_GT)            \
    X(OP_GE)            \
    X(OP_LT)            \
    X(OP_LE)            \
    X(OP_IS)            \
    X(OP_POW)           \
    X(OP_GET_FIELD)     \
    X(OP_SET_FIELD)     \
    X(OP_SUBSCR_SET)    \
    X(OP_SUBSCR_GET)    \
    X(OP_CALL)          \
    X(OP_CALL_0)        \
    X(OP_CALL_1)        \
    X(OP_CALL_2)        \
    X(OP_CALL_3)        \
    X(OP_CALL_4)        \
    X(OP_CALL_5)        \
    X(OP_CALL_6)        \
    X(OP_CALL_7)        \
    X(OP_CALL_8)        \
    X(OP_CALL_9)        \
    X(OP_CALL_10)       \
    X(OP_INVOKE)        \
    X(OP_INVOKE_0)      \
    X(OP_INVOKE_1)      \
    X(OP_INVOKE_2)      \
    X(OP_INVOKE_3)      \
    X(OP_INVOKE_4)      \
    X(OP_INVOKE_5)      \
    X(OP_INVOKE_6)      \
    X(OP_INVOKE_7)      \
    X(OP_INVOKE_8)      \
    X(OP_INVOKE_9)      \
    X(OP_INVOKE_10)     \
    X(OP_SUPER)         \
    X(OP_SUPER_0)       \
    X(OP_SUPER_1)       \
    X(OP_SUPER_2)       \
    X(OP_SUPER_3)       \
    X(OP_SUPER_4)       \
    X(OP_SUPER_5)       \
    X(OP_SUPER_6)       \
    X(OP_SUPER_7)       \
    X(OP_SUPER_8)       \
    X(OP_SUPER_9)       \
    X(OP_SUPER_10)      \
    X(OP_JUMP)          \
    X(OP_JUMPT)         \
    X(OP_JUMPF)         \
    X(OP_IMPORT)        \
    X(OP_IMPORT_AS)     \
    X(OP_IMPORT_FROM)   \
    X(OP_IMPORT_NAME)   \
    X(OP_NEW_LIST)      \
    X(OP_APPEND_LIST)   \
    X(OP_NEW_TUPLE)     \
    X(OP_CLOSURE)       \
    X(OP_NEW_CLASS)     \
    X(OP_NEW_SUBCLASS)  \
    X(OP_DEF_METHOD)    \
    X(OP_NAT_METHOD)    \
    X(OP_GET_CONST)     \
    X(OP_GET_LOCAL)     \
    X(OP_GET_UPVALUE)   \
    X(OP_GET_GLOBAL)    \
    X(OP_SET_LOCAL)     \
    X(OP_SET_UPVALUE)   \
    X(OP_SET_GLOBAL)    \
    X(OP_DEFINE_GLOBAL) \
    X(OP_NATIVE)        \
    X(OP_RETURN)        \
    X(OP_NULL)          \
    X(OP_SETUP_EXCEPT)  \
    X(OP_SETUP_ENSURE)  \
    X(OP_ENSURE_END)    \
    X(OP_POP_HANDLER)   \
    X(OP_RAISE)         \
    X(OP_POP)           \
    X(OP_CLOSE_UPVALUE) \
    X(OP_DUP)           \
    X(OP_UNPACK)        \
    X(OP_SIGN_CONT)     \
    X(OP_SIGN_BRK)

DEFINE_ENUM(Opcode, OPCODE);
DECLARE_TO_STRING(Opcode);

int opcodeArgsNumber(Opcode op);

#endif
