#include "memory.h"
#include "lzarena.h"
#include <stdio.h>

void *memory_arena_alloc(size_t size, void *ctx){
    AllocatorContext *actx = (AllocatorContext *)ctx;
    jmp_buf *err_buf = actx->err_buf;
    void *behind_allocator = actx->behind_allocator;
    void *ptr = LZARENA_ALLOC(size, behind_allocator);

    if(!ptr){
        if(err_buf){
            longjmp(*err_buf, 1);
        }

        fprintf(stderr, "Out of memory");
    }

    return ptr;
}

void *memory_arena_realloc(void *ptr, size_t old_size, size_t new_size, void *ctx){
    AllocatorContext *actx = (AllocatorContext *)ctx;
    jmp_buf *err_buf = actx->err_buf;
    void *behind_allocator = actx->behind_allocator;
    void *new_ptr = LZARENA_REALLOC(ptr, old_size, new_size, behind_allocator);

    if(!new_ptr){
        if(err_buf){
            longjmp(*err_buf, 1);
        }

        fprintf(stderr, "Out of memory");
    }

    return new_ptr;
}

void memory_arena_dealloc(void *ptr, size_t size, void *ctx){
    // do nothing
}