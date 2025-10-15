#ifndef INSTRUCTION_H
#define INSTRUCTION_H

#include "types.h"
#include "location.h"
#include "token.h"

typedef enum instruction_type{
    ADD_INSTRUCTION_TYPE,
    IDIV_INSTRUCTION_TYPE,
    MOV_INSTRUCTION_TYPE,
    IMUL_INSTRUCTION_TYPE,
    SUB_INSTRUCTION_TYPE,
    RET_INSTRUCTION_TYPE,
    XOR_INSTRUCTION_TYPE,
}InstructionType;

typedef struct empty_instruction{
    Token *token;
}EmptyInstruction;

typedef struct unary_instruction{
    Location *src_location;
    Token *src_token;
}UnaryInstruction;

typedef struct binary_instruction{
    Location *dst_location;
    Location *src_location;
    Token *dst_token;
    Token *src_token;
}BinaryInstruction;

typedef struct instruction{
    InstructionType type;
    void *sub_instruction;
}Instruction;

#endif