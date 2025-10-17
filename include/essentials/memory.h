#ifndef MEMORY_H
#define MEMORY_H

#include "lzstack.h"
#include "lzohtable.h"

#include <stddef.h>
#include <setjmp.h>
#include <string.h>

void *memory_arena_alloc(size_t size, void *ctx);
void *memory_arena_realloc(void *ptr, size_t old_size, size_t new_size, void *ctx);
void memory_arena_dealloc(void *ptr, size_t size, void *ctx);

typedef struct allocator_context{
    jmp_buf *err_buf;
    void *behind_allocator;
}AllocatorContext;

typedef struct allocator{
    void *ctx;
    void *(*alloc)(size_t size, void *ctx);
    void *(*realloc)(void *ptr, size_t old_size, size_t new_size, void *ctx);
    void (*dealloc)(void *ptr, size_t size, void *ctx);
    jmp_buf *err_buf;
}Allocator;

#define MEMORY_INIT_ALLOCATOR(_ctx, _alloc, _realloc, _dealloc, _allocator){ \
    (_allocator)->ctx = (_ctx);                                              \
    (_allocator)->alloc = (_alloc);                                          \
    (_allocator)->realloc = (_realloc);                                      \
    (_allocator)->dealloc = (_dealloc);                                      \
}

#define MEMORY_CALC_SIZE(_type, _count)                                (sizeof(_type) * (_count))
#define MEMORY_ALLOC(_type, _count, _allocator)                        ((_allocator)->alloc(MEMORY_CALC_SIZE(_type, _count), (_allocator)->ctx))
#define MEMORY_REALLOC(_type, old_count, _new_count, _ptr, _allocator) ((_type *)((_allocator)->realloc(_ptr, (sizeof(_type) * old_count), (sizeof(_type) * _new_count), (_allocator)->ctx)))
#define MEMORY_DEALLOC(_ptr, _type, _count, _allocator)                ((_allocator)->dealloc((_ptr), MEMORY_CALC_SIZE(_type, _count), (_allocator)->ctx))

#define MEMORY_NEW(_allocator, _type, ...)                             ((_type *)(memcpy(MEMORY_ALLOC(_type, 1, _allocator), &(_type){__VA_ARGS__}, sizeof(_type))))
#define MEMORY_DYNARR_TYPE(_allocator, _type)                          (dynarr_create(sizeof(_type), (DynArrAllocator *)(_allocator)))
#define MEMORY_DYNARR_PTR(_allocator)                                  (DYNARR_CREATE_PTR((DynArrAllocator *)(_allocator)))
#define MEMORY_LZSTACK(_allocator)                                     (lzstack_create((LZStackAllocator *)(_allocator)))
#define MEMORY_LZOHTABLE(_allocator)                                   (lzohtable_create(16, 0.85, (LZOHTableAllocator *)(_allocator)))

#endif