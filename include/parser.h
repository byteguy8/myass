#ifndef PARSER_H
#define PARSER_H

#include "essentials/dynarr.h"
#include "essentials/memory.h"
#include <setjmp.h>

typedef struct parser{
    jmp_buf   err_buf;
    size_t    current;
    DynArr    *tokens;
    Allocator *allocator;
}Parser;

Parser *parser_create(Allocator *allocator);

void parser_destroy(Parser *parser);

int parser_parse(Parser *parser, DynArr *tokens, DynArr *instructions);

#endif