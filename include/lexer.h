#ifndef LEXER_H
#define LEXER_H

#include "essentials/memory.h"
#include "types.h"
#include "essentials/dynarr.h"
#include "essentials/lzohtable.h"
#include <setjmp.h>

typedef struct lexer{
    size_t    start_line_offset;
    size_t    end_line_offset;
    size_t    start_line;
    size_t    end_line;
    size_t    start;
    size_t    current;
    jmp_buf   err_buf;
    BStr      *code;
    LZOHTable *registers_keywords;
    LZOHTable *instructions_keywords;
    DynArr    *tokens;
    Allocator *allocator;
}Lexer;

Lexer *lexer_create(Allocator *allocator);

void lexer_destroy(Lexer *lexer);

int lexer_lex(
    Lexer *lexer,
    LZOHTable *registers_keywords,
    LZOHTable *instructions_keywords,
    BStr *code,
    DynArr *tokens
);

#endif