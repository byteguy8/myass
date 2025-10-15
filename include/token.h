#ifndef TOKEN_H
#define TOKEN_H

#include <stddef.h>

typedef enum token_type{
    COMMA_TOKEN_TYPE, MINUS_TOKEN_TYPE,

    DWORD_TYPE_TOKEN_TYPE,

    REGISTER_TOKEN_TYPE,

    ADD_TOKEN_TYPE,
    IDIV_TOKEN_TYPE,
    MOV_TOKEN_TYPE,
    IMUL_TOKEN_TYPE,
    SUB_TOKEN_TYPE,
    RET_TOKEN_TYPE,
    XOR_TOKEN_TYPE,

    EOF_TOKEN_TYPE
}TokenType;

typedef struct token{
    int start_line;
    int end_line;
    int start_col;
    int end_col;
    int offset_start;
    int offset_end;
    size_t lexeme_len;
    size_t literal_size;
    TokenType type;
    const char *lexeme;
    const void *literal;
}Token;

#endif