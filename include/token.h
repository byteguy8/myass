#ifndef TOKEN_H
#define TOKEN_H

#include <stddef.h>
#include <stdint.h>

typedef enum token_type{
    COMMA_TOKEN_TYPE, MINUS_TOKEN_TYPE, COLON_TOKEN_TYPE,

    DWORD_TYPE_TOKEN_TYPE,

    REGISTER_TOKEN_TYPE,

    ADD_TOKEN_TYPE,
    CMP_TOKEN_TYPE,
    IDIV_TOKEN_TYPE,
    IMUL_TOKEN_TYPE,
    JE_TOKEN_TYPE,
    JG_TOKEN_TYPE,
    JL_TOKEN_TYPE,
    JGE_TOKEN_TYPE,
    JLE_TOKEN_TYPE,
    JMP_TOKEN_TYPE,
    MOV_TOKEN_TYPE,
    SUB_TOKEN_TYPE,
    RET_TOKEN_TYPE,
    XOR_TOKEN_TYPE,

    IDENTIFIER_TOKEN_TYPE,

    EOF_TOKEN_TYPE
}TokenType;

typedef struct token{
    int32_t    start_line;
    int32_t    end_line;
    int32_t    start_col;
    int32_t    end_col;
    size_t     offset_start;
    size_t     offset_end;
    size_t     lexeme_len;
    size_t     literal_size;
    TokenType  type;
    const char *lexeme;
    const void *literal;
}Token;

#endif
