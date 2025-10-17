#include "lzstack.h"
#include <stdlib.h>

#define NODE_SIZE sizeof(LZStackNode)

static void *lzalloc(const LZStackAllocator *allocator, size_t size);
static void *lzrealloc(const LZStackAllocator *allocator, void *ptr, size_t old_size, size_t new_size);
static void lzdealloc(const LZStackAllocator *allocator, void *ptr, size_t size);

#define MEMORY_ALLOC(_allocator, _type, _count)                         ((_type *)lzalloc((_allocator), sizeof(_type) * (_count)))
#define MEMORY_REALLOC(_allocator, _ptr, _type, _old_count, _new_count) ((_type *)(lzrealloc((_allocator), (_ptr), sizeof(_type) * (_old_count), sizeof(_type) * (_new_count))))
#define MEMORY_DEALLOC(_allocator, _ptr, _type, _count)                 (lzdealloc((_allocator), (_ptr), sizeof(_type) * (_count)))

inline void *lzalloc(const LZStackAllocator *allocator, size_t size){
    return allocator ? allocator->alloc(size, allocator->ctx) : malloc(size);
}

inline void *lzrealloc(const LZStackAllocator *allocator, void *ptr, size_t old_size, size_t new_size){
    return allocator ? allocator->realloc(ptr, old_size, new_size, allocator->ctx) : realloc(ptr, new_size);
}

inline void lzdealloc(const LZStackAllocator *allocator, void *ptr, size_t size){
    if(allocator){
        allocator->dealloc(ptr, size, allocator->ctx);
    }else{
        free(ptr);
    }
}

LZStack *lzstack_create(LZStackAllocator *allocator){
    LZStack *stack = MEMORY_ALLOC(allocator, LZStack, 1);

    if(!stack){
        return NULL;
    }

    stack->len = 0;
    stack->top = NULL;
    stack->allocator = allocator;

    return stack;
}

void lzstack_destroy(LZStack *stack){
    if(!stack){
        return;
    }

    LZStackAllocator *allocator = stack->allocator;
    LZStackNode *current = stack->top;
    LZStackNode *prev = NULL;

    while (current){
        prev = current->prev;
        MEMORY_DEALLOC(allocator, current, LZStackNode, 1);
        current = prev;
    }

    MEMORY_DEALLOC(allocator, stack, LZStack, 1);
}

void *lzstack_peek(LZStack *stack){
    LZStackNode *top_node = stack->top;
    return top_node ? top_node->value : NULL;
}

int lzstack_push(void *value, LZStack *stack){
    LZStackNode *node = MEMORY_ALLOC(stack->allocator, LZStackNode, 1);

    if(!node){
        return 1;
    }

    node->prev = stack->top;
    node->value = value;

    stack->len++;
    stack->top = node;

    return 0;
}

void *lzstack_pop(LZStack *stack){
    LZStackNode *top = stack->top;

    if(top){
        stack->len--;
        stack->top = top->prev;
        void *value = top->value;

        MEMORY_DEALLOC(stack->allocator, top, LZStackNode, 1);

        return value;
    }

    return NULL;
}