#ifndef LZSTACK_H
#define LZSTACK_H

#include <stddef.h>

typedef struct lzstack_allocator{
    void *ctx;
    void *(*alloc)(size_t size, void *ctx);
    void *(*realloc)(void *ptr, size_t old_size, size_t new_size, void *ctx);
    void (*dealloc)(void *ptr, size_t size, void *ctx);
}LZStackAllocator;

typedef struct lzstack_node{
    void *value;
    struct lzstack_node *prev;
}LZStackNode;

typedef struct lzstack{
    size_t len;
    LZStackNode *top;
    LZStackAllocator *allocator;
}LZStack;

LZStack *lzstack_create(LZStackAllocator *allocator);

void lzstack_destroy(LZStack *stack);

void *lzstack_peek(LZStack *stack);

#define LZSTACK_PEEK_AS(_type, _stack)((_type *)lzstack_peek(_stack))

int lzstack_push(void *value, LZStack *stack);

void *lzstack_pop(LZStack *stack);

#endif