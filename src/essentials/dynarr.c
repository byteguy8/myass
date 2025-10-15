#include "dynarr.h"
#include <assert.h>

// PRIVATE INTERFACE
static void *lzalloc(size_t size, DynArrAllocator *allocator);
static void *lzrealloc(void *ptr, size_t old_size, size_t new_size, DynArrAllocator *allocator);
static void lzdealloc(void *ptr, size_t size, DynArrAllocator *allocator);

#define MEMORY_ALLOC(_type, _count, _allocator)((_type *)lzalloc(sizeof(_type) * (_count), (_allocator)))
#define MEMORY_REALLOC(_ptr, _type, _old_count, _new_count, _allocator)((_type *)(lzrealloc((_ptr), sizeof(_type) * (_old_count), sizeof(_type) * (_new_count), (_allocator))))
#define MEMORY_DEALLOC(_ptr, _type, _count, _allocator)(lzdealloc((_ptr), sizeof(_type) * (_count), (_allocator)))

static inline size_t padding_size(size_t item_size);
static int grow(DynArr *dynarr);
static int grow_by(size_t by, DynArr *dynarr);
static int shrink(DynArr *dynarr);
static inline void *get_slot(size_t idx, DynArr *dynarr);
static inline void move_items(size_t from, size_t to, DynArr *dynarr){
    memmove(
        get_slot(to, dynarr),
        get_slot(from, dynarr),
        (dynarr->used - from + 1) * dynarr->fsize
    );
}

// PRIVATE IMPLEMENTATION
void *lzalloc(size_t size, DynArrAllocator *allocator){
    return allocator ? allocator->alloc(size, allocator->ctx) : malloc(size);
}

void *lzrealloc(void *ptr, size_t old_size, size_t new_size, DynArrAllocator *allocator){
    return allocator ? allocator->realloc(ptr, old_size, new_size, allocator->ctx) : realloc(ptr, new_size);
}

void lzdealloc(void *ptr, size_t size, DynArrAllocator *allocator){
    if (allocator){
        allocator->dealloc(ptr, size, allocator->ctx);
    }else{
        free(ptr);
    }
}

static inline size_t padding_size(size_t item_size){
    size_t modulo = item_size & (DYNARR_ALIGNMENT - 1);
    return modulo == 0 ? 0 : DYNARR_ALIGNMENT - modulo;
}

static int grow(DynArr *dynarr){
    size_t item_size = dynarr->fsize;
    size_t old_count = dynarr->count;
    size_t new_count = old_count == 0 ? DYNARR_DEFAULT_GROW_SIZE : old_count * 2;
    size_t old_size = old_count * item_size;
    size_t new_size = new_count * item_size;

    void *new_items = MEMORY_REALLOC(dynarr->items, char, old_size, new_size, dynarr->allocator);

    if (!new_items){
        return 1;
    }

    dynarr->count = new_count;
    dynarr->items = new_items;

    return 0;
}

static int grow_by(size_t by, DynArr *dynarr){
    size_t item_size = dynarr->fsize;
    size_t old_count = dynarr->count;
    size_t new_count = old_count + by;
    size_t old_size = old_count * item_size;
    size_t new_size = new_count * item_size;

    void *new_items = MEMORY_REALLOC(dynarr->items, char, old_size, new_size, dynarr->allocator);

    if (!new_items){
        return 1;
    }

    dynarr->count = new_count;
    dynarr->items = new_items;

    return 0;
}

static int shrink(DynArr *dynarr){
    size_t item_size = dynarr->fsize;
    size_t old_count = dynarr->count;
    size_t new_count = old_count / 2;
    size_t old_size = old_count * item_size;
    size_t new_size = new_count * item_size;

    void *new_items = MEMORY_REALLOC(dynarr->items, char, old_size, new_size, dynarr->allocator);

    if (!new_items){
        return 1;
    }

    dynarr->count = new_count;
    dynarr->items = new_items;

    return 0;
}

static inline void *get_slot(size_t idx, DynArr *dynarr){
    return ((char *)(dynarr->items)) + (idx * dynarr->fsize);
}

// public implementation
DynArr *dynarr_create(size_t item_size, DynArrAllocator *allocator){
    DynArr *dynarr = MEMORY_ALLOC(DynArr, 1, allocator);

    if(!dynarr){
        return NULL;
    }

    dynarr->used = 0;
    dynarr->count = 0;
    dynarr->rsize = item_size;
    dynarr->fsize = padding_size(item_size) + item_size;
    dynarr->items = NULL;
    dynarr->allocator = allocator;

    return dynarr;
}

DynArr *dynarr_create_by(size_t item_size, size_t item_count, DynArrAllocator *allocator){
    size_t fsize = padding_size(item_size) + item_size;
    void *items = MEMORY_ALLOC(char, fsize * item_count, allocator);
    DynArr *dynarr = MEMORY_ALLOC(DynArr, 1, allocator);

    if(!items || !dynarr){
        MEMORY_DEALLOC(items, char, fsize * item_count, allocator);
        MEMORY_DEALLOC(dynarr, DynArr, 1, allocator);
        return NULL;
    }

    dynarr->used = 0;
    dynarr->count = item_count;
    dynarr->rsize = item_size;
    dynarr->fsize = fsize;
    dynarr->items = items;
    dynarr->allocator = allocator;

    return dynarr;
}

void dynarr_destroy(DynArr *dynarr){
    if (!dynarr){
        return;
    }

    DynArrAllocator *allocator = dynarr->allocator;

    MEMORY_DEALLOC(dynarr->items, char, dynarr->fsize * dynarr->count, allocator);
    MEMORY_DEALLOC(dynarr, DynArr, 1, allocator);
}

inline size_t dynarr_available(DynArr *dynarr){
    return dynarr->count - dynarr->used;
}

inline int dynarr_reduce(DynArr *dynarr){
    if(DYNARR_LEN(dynarr) < dynarr->count / 2){
        return !shrink(dynarr);
    }

    return 0;
}

void dynarr_reverse(DynArr *dynarr){
    size_t fsize = dynarr->fsize;
    size_t len = DYNARR_LEN(dynarr);
    size_t until = DYNARR_LEN(dynarr) / 2;

    for (size_t left_index = 0; left_index < until; left_index++){
        size_t right_index = len - 1 - left_index;
        char *left = get_slot(left_index, dynarr);
        char *right = get_slot(right_index, dynarr);
        char temp_item[fsize];

        memcpy(temp_item, left, fsize);
        dynarr_set_at(left_index, right, dynarr);
        dynarr_set_at(right_index, temp_item, dynarr);
    }
}

inline void dynarr_sort(int (*comparator)(const void *a, const void *b), DynArr *dynarr){
    qsort(dynarr->items, dynarr->used, dynarr->fsize, comparator);
}

int dynarr_find(void *item, int (*comparator)(const void *a, const void *b), DynArr *dynarr){
    int left = 0;
    int right = DYNARR_LEN(dynarr) == 0 ? 0 : DYNARR_LEN(dynarr) - 1;

    while (left <= right){
        int middle_index = (left + right) / 2;
        void *middle = get_slot((size_t)middle_index, dynarr);
        int comparition = comparator(middle, item);

        if(comparition < 0){
            left = middle_index + 1;
        }else if(comparition > 0){
            right = middle_index - 1;
        }else{
            return middle_index;
        }
    }

    return -1;
}

inline void *dynarr_get_raw(size_t idx, DynArr *dynarr){
    if(idx >= dynarr->used){
        return NULL;
    }

    return get_slot(idx, dynarr);
}

inline void *dynarr_get_ptr(size_t idx, DynArr *dynarr){
    size_t rsize = dynarr->rsize;

    assert(rsize == sizeof(uintptr_t) && "Item size must be equals to pointer size");

    if(idx >= dynarr->used){
        return NULL;
    }

    return (void *)(*(uintptr_t *)get_slot(idx, dynarr));
}

inline int dynarr_set_at(size_t idx, void *item, DynArr *dynarr){
    if(idx >= dynarr->used){
        return 1;
    }

    memmove(get_slot(idx, dynarr), item, dynarr->rsize);

    return 0;
}

inline void dynarr_set_ptr(size_t idx, void *ptr, DynArr *dynarr){
    size_t rsize = dynarr->rsize;

    assert(rsize == sizeof(uintptr_t) && "Item size must be equals to pointer size");

    uintptr_t iptr = (uintptr_t)ptr;

    memmove(get_slot(idx, dynarr), &iptr, rsize);
}

inline int dynarr_insert(void *item, DynArr *dynarr){
    if (dynarr->used >= dynarr->count && grow(dynarr)){
        return 1;
    }

    memmove(get_slot(dynarr->used++, dynarr), item, dynarr->rsize);

    return 0;
}

int dynarr_insert_at(size_t idx, void *item, DynArr *dynarr){
    if(DYNARR_LEN(dynarr) == 0){
        if(idx > 0){
            return 1;
        }

        return dynarr_insert(item, dynarr);
    }

    if (dynarr->used >= dynarr->count && grow(dynarr)){
        return 1;
    }

    move_items(idx, idx + 1, dynarr);
    dynarr_set_at(idx, item, dynarr);

    dynarr->used++;

    return 0;
}

inline int dynarr_insert_ptr(void *ptr, DynArr *dynarr){
    size_t rsize = dynarr->rsize;

    assert(rsize == sizeof(uintptr_t) && "Item size must be equals to pointer size");

    uintptr_t iptr = (uintptr_t)ptr;

    return dynarr_insert(&iptr, dynarr);
}

inline int dynarr_insert_ptr_at(size_t index, void *ptr, DynArr *dynarr){
    size_t rsize = dynarr->rsize;

    assert(rsize == sizeof(uintptr_t) && "Item size must be equals to pointer size");

    uintptr_t iptr = (uintptr_t)ptr;

    return dynarr_insert_at(index, &iptr, dynarr);
}

int dynarr_append(DynArr *from, DynArr *to){
    assert(to->rsize == from->rsize && "Expect both list to have the same item size");

    size_t from_len = DYNARR_LEN(from);

    if(from_len == 0){
        return 1;
    }

    size_t to_len = DYNARR_LEN(to);
    size_t to_available = dynarr_available(to);
    size_t to_start_idx = to_len == 0 ? 0 : to_len - 1;

    if(from_len <= to_available){
        memmove(
            get_slot(to_start_idx, to),
            get_slot(0, from),
            from->fsize * from_len
        );

        to->used += from_len;

        return 0;
    }

    size_t required = from_len - to_available;

    if(grow_by(required, to)){
        return 1;
    }

    memmove(
        get_slot(to_start_idx, to),
        get_slot(0, from),
        from->fsize * from_len
    );

    to->used += from_len;

    return 0;
}

DynArr *dynarr_append_new(DynArr *a_dynarr, DynArr *b_dynarr, DynArrAllocator *allocator){
    size_t a_rsize = a_dynarr->rsize;
    size_t b_rsize = b_dynarr->rsize;

    assert(a_rsize == b_rsize && "Both DynArr's item size must be the same");

    size_t fsize = a_dynarr->fsize;
    size_t a_len = DYNARR_LEN(a_dynarr);
    size_t b_len = DYNARR_LEN(b_dynarr);
    size_t c_len = a_len + b_len;
    DynArr *c_dynarr = dynarr_create_by(a_rsize, c_len, allocator);

    if(!c_dynarr){
        return NULL;
    }

    memcpy(c_dynarr->items, a_dynarr->items, fsize * a_len);
    memcpy(get_slot(a_len, c_dynarr), b_dynarr->items, fsize * b_len);

    c_dynarr->used = c_len;

    return c_dynarr;
}

inline void dynarr_remove_index(size_t idx, DynArr *dynarr){
    if(idx < DYNARR_LEN(dynarr) - 1){
        move_items(idx + 1, idx, dynarr);
    }

    dynarr->used--;
}

inline void dynarr_remove_all(DynArr *dynarr){
    dynarr->used = 0;
}