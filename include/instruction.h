#ifndef INSTRUCTION_H
#define INSTRUCTION_H

#include "types.h"
#include "location.h"
#include "token.h"

#include <stddef.h>

typedef enum instruction_type{
    LABEL_INSTRUCTION_TYPE,

    ADD_INSTRUCTION_TYPE,
    CALL_INSTRUCTION_TYPE,
    CMP_INSTRUCTION_TYPE,
    IDIV_INSTRUCTION_TYPE,
    IMUL_INSTRUCTION_TYPE,
    JE_INSTRUCTION_TYPE,
    JG_INSTRUCTION_TYPE,
    JL_INSTRUCTION_TYPE,
    JGE_INSTRUCTION_TYPE,
    JLE_INSTRUCTION_TYPE,
    JMP_INSTRUCTION_TYPE,
    MOV_INSTRUCTION_TYPE,
    POP_INSTRUCTION_TYPE,
    PUSH_INSTRUCTION_TYPE,
    SUB_INSTRUCTION_TYPE,
    RET_INSTRUCTION_TYPE,
    XOR_INSTRUCTION_TYPE,
}InstructionType;

typedef struct empty_instruction{
    Token *token;
}EmptyInstruction;

typedef struct unary_instruction{
    Location *location;
    Token *instruction_token;
    Token *operand_token;
}UnaryInstruction;

typedef struct binary_instruction{
    Location *dst_location;
    Location *src_location;
    Token *instruction_token;
    Token *dst_token;
    Token *src_token;
}BinaryInstruction;

typedef struct instruction{
	size_t offset;
	size_t len;
    InstructionType type;
    void *sub_instruction;
}Instruction;

#endif
