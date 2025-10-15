// Humble implementation of a dynamic array

#ifndef DYNARR_H
#define DYNARR_H

#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#ifndef DYNARR_ALIGNMENT
#define DYNARR_ALIGNMENT 8
#endif

#ifndef DYNARR_DEFAULT_GROW_SIZE
#define DYNARR_DEFAULT_GROW_SIZE 8
#endif

typedef struct dynarr_allocator{
    void *ctx;
    void *(*alloc)(size_t size, void *ctx);
    void *(*realloc)(void *ptr, size_t old_size, size_t new_size, void *ctx);
    void (*dealloc)(void *ptr, size_t size, void *ctx);
}DynArrAllocator;

typedef struct dynarr{
    size_t count;
    size_t used;
    size_t rsize;
    size_t fsize;
    char *items;
    DynArrAllocator *allocator;
}DynArr;

// PUBLIC INTERFACE DYNARR
DynArr *dynarr_create(size_t item_size, DynArrAllocator *allocator);
#define DYNARR_CREATE_TYPE(_type, _allocator)(dynarr_create(sizeof(_type), _allocator))
#define DYNARR_CREATE_PTR(_allocator) DYNARR_CREATE_TYPE(uintptr_t, _allocator)
DynArr *dynarr_create_by(size_t item_size, size_t item_count, DynArrAllocator *allocator);
#define DYNARR_CREATE_TYPE_BY(_type, _count, _allocator)(dynarr_create_by(sizeof(_type), _count, _allocator))
#define DYNARR_CREATE_PTR_BY(_count, _allocator) DYNARR_CREATE_TYPE_BY(uintptr_t, _count, _allocator)
void dynarr_destroy(DynArr *dynarr);

#define DYNARR_LEN(dynarr)((dynarr)->used)
size_t dynarr_available(DynArr *dynarr);
int dynarr_reduce(DynArr *dynarr);
void dynarr_reverse(DynArr *dyarr);
void dynarr_sort(int (*comparator)(const void *a, const void *b), DynArr *dynarr);
int dynarr_find(void *item, int (*comparator)(const void *a, const void *b), DynArr *dynarr);

void *dynarr_get_raw(size_t idx, DynArr *dynarr);
#define DYNARR_GET_AS(_as, _idx, _dynarr)(*(_as *)(dynarr_get_raw(_idx, _dynarr)))
void *dynarr_get_ptr(size_t idx, DynArr *dynarr);
#define DYNARR_GET_PTR_AS(_as, _idx, _dynarr) ((_as *)dynarr_get_ptr(_idx, _dynarr))

int dynarr_set_at(size_t idx, void *item, DynArr *dynarr);
void dynarr_set_ptr(size_t idx, void *ptr, DynArr *dynarr);

int dynarr_insert(void *item, DynArr *dynarr);
int dynarr_insert_at(size_t index, void *item, DynArr *dynarr);
int dynarr_insert_ptr(void *ptr, DynArr *dynarr);
int dynarr_insert_ptr_at(size_t index, void *ptr, DynArr *dynarr);
int dynarr_append(DynArr *from, DynArr *to);
DynArr *dynarr_append_new(DynArr *a_dynarr, DynArr *b_dynarr, DynArrAllocator *allocator);

void dynarr_remove_index(size_t idx, DynArr *dynarr);
void dynarr_remove_all(DynArr *dynarr);

#endif