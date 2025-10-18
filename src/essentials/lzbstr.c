#include "lzbstr.h"
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>

static inline void *lzalloc(size_t size, LZBStrAllocator *allocator){
    return allocator ? allocator->alloc(size, allocator->ctx) : malloc(size);
}

static inline void *lzrealloc(void *ptr, size_t old_size, size_t new_size, LZBStrAllocator *allocator){
    return allocator ? allocator->realloc(ptr, old_size, new_size, allocator->ctx) : realloc(ptr, new_size);
}

static inline void lzdealloc(void *ptr, size_t size, LZBStrAllocator *allocator){
    if(allocator){
        allocator->dealloc(ptr, size, allocator->ctx);
    }else{
        free(ptr);
    }
}

#define MEMORY_ALLOC(_type, _count, _allocator)((_type *)lzalloc(sizeof(_type) * (_count), (_allocator)))
#define MEMORY_REALLOC(_ptr, _type, _old_count, _new_count, _allocator)((_type *)(lzrealloc((_ptr), sizeof(_type) * (_old_count), sizeof(_type) * (_new_count), (_allocator))))
#define MEMORY_DEALLOC(_ptr, _type, _count, _allocator)(lzdealloc((_ptr), sizeof(_type) * (_count), (_allocator)))

static inline size_t max(size_t a, size_t b){
    return ((((size_t) 0) - (a >= b)) & a) | ((((size_t) 0) - (a < b)) & b);
}

static inline size_t min(size_t a, size_t b){
    return ((((size_t) 0) - (a <= b)) & a) | ((((size_t) 0) - (a > b)) & b);
}

static inline uint64_t next_pow2m1(uint64_t x) {
    x |= x >> 1;
	x |= x >> 2;
	x |= x >> 4;
	x |= x >> 8;
	x |= x >> 16;
	x |= x >> 32;
    return x;
}

static inline uint64_t next_pow2(uint64_t x) {
	return next_pow2m1(x-1)+1;
}

static inline size_t available_space(LZBStr *str){
    return str->buff_len - str->offset;
}

static int grow(char multiplier, size_t required, LZBStr *str){
    size_t buff_len = str->buff_len;
    size_t new_buff_len = ((size_t)next_pow2((uint64_t)(buff_len + required))) * multiplier;
    void *new_buff = MEMORY_REALLOC(str->buff, char, str->buff_len, new_buff_len, str->allocator);

    if(!new_buff){
        return 1;
    }

    str->buff_len = new_buff_len;
    str->buff = new_buff;

    return 0;
}

LZBStr *lzbstr_create(LZBStrAllocator *allocator){
    LZBStr *str = MEMORY_ALLOC(LZBStr, 1, allocator);

    if(!str){
        return NULL;
    }

    str->offset = 0;
    str->buff_len = 0;
    str->buff = NULL;
    str->allocator = allocator;

    return str;
}

void lzbstr_destroy(LZBStr *str){
    if(!str){
        return;
    }

    LZBStrAllocator *allocator = str->allocator;

    MEMORY_DEALLOC(str->buff, char, str->buff_len, allocator);
    MEMORY_DEALLOC(str, LZBStr, 1, allocator);
}

char *lzbstr_destroy_save_buff(LZBStr *str){
    if(!str){
        return NULL;
    }

    char *buff = str->buff;
    MEMORY_DEALLOC(str, LZBStr, 1, str->allocator);

    return buff;
}

size_t lzbstr_available_space(LZBStr *str){
    return str->buff_len - str->offset;
}

void lzbstr_reset(LZBStr *str){
    str->offset = 0;
    str->buff[0] = 0;
}

int lzbstr_grow_by(size_t by, LZBStr *str){
    return grow(1, by + available_space(str), str);
}

char *lzbstr_rclone_buff_rng(size_t from, size_t to, LZBStrAllocator *allocator, LZBStr *str, size_t *out_len){
    if(str->offset == 0){
        return NULL;
    }

    assert(from < to && "Expect [from, to) interval");
    assert(to <= str->offset && "Expect 'to' be less or equals to 'offset'");

    size_t len = to - from;
    char *cloned_buff = MEMORY_ALLOC(char, len + 1, allocator);

    if(!cloned_buff){
        return NULL;
    }

    memcpy(cloned_buff, str->buff + from, len);
    cloned_buff[len] = 0;

    if(out_len){
        *out_len = len;
    }

    return cloned_buff;
}

char *lzbstr_rclone_buff(LZBStrAllocator *allocator, LZBStr *str, size_t *out_len){
    return lzbstr_rclone_buff_rng(0, LZBSTR_LEN(str), allocator, str, out_len);
}

int lzbstr_append(char *rstr, LZBStr *str){
    size_t rstr_len = strlen(rstr);
    size_t available = available_space(str);
    size_t max_value = max(rstr_len, available);
    size_t min_value = min(rstr_len, available);
    size_t required = max_value - min_value;

    if(rstr_len > available && grow(2, max(required, 16), str)){
        return 1;
    }

    memcpy(str->buff + str->offset, rstr, rstr_len);
    str->offset += rstr_len;
    str->buff[str->offset] = 0;

    return 0;
}

int lzbstr_append_args(LZBStr *str, char *fmt, ...){
    va_list args;

    va_start(args, fmt);
    int raw_rstr_len = vsnprintf(NULL, 0, fmt, args);
    assert(raw_rstr_len != -1 && "No hay ma na que hacel");

    size_t rstr_len = (size_t)raw_rstr_len;
    size_t available = available_space(str);
    size_t max_value = max(rstr_len, available);
    size_t min_value = min(rstr_len, available);
    size_t required = max_value - min_value;

    if(rstr_len > available && grow(2, max(required, 16), str)){
        return 1;
    }

    va_start(args, fmt);
    vsnprintf(str->buff + str->offset, rstr_len + 1, fmt, args);
    str->offset += rstr_len;

    va_end(args);

    return 0;
}

int lzbstr_insert_args(LZBStr *str, size_t from, char *fmt, ...){
    size_t old_offset = str->offset;

    if(old_offset == 0 && from > 0){
        return 1;
    }

    va_list args;

    va_start(args, fmt);
    int raw_rstr_len = vsnprintf(NULL, 0, fmt, args);
    assert(raw_rstr_len != -1 && "No hay ma na que hacel");

    size_t rstr_len = (size_t)raw_rstr_len;
    size_t available = available_space(str);
    size_t max_value = max(rstr_len, available);
    size_t min_value = min(rstr_len, available);
    size_t required = max_value - min_value;

    if(rstr_len > available && grow(2, max(required, 16), str)){
        return 1;
    }

    char c = str->buff[from];
    size_t left_len = old_offset - from;
    memmove(str->buff + from + rstr_len, str->buff + from, left_len);

    va_start(args, fmt);
    vsnprintf(str->buff + from, rstr_len + 1, fmt, args);
    str->offset = from + rstr_len + left_len;
    str->buff[from + rstr_len] = (0 - (old_offset > 0)) & c;

    va_end(args);

    return 0;
}

int lzbstr_remove(size_t from, size_t to, LZBStr *str){
    if(str->offset == 0){
        return 1;
    }

    assert(from < to && "Expect [from, to) interval");
    assert(to <= str->offset && "Expect 'to' be less or equals to 'offset'");

    size_t len = to - from;

    if(to == str->offset){
        str->offset = from;
        str->buff[from] = 0;
    }else{
        size_t left_len = str->offset - to;
        size_t new_offset = from + left_len;

        memmove(str->buff + from, str->buff + to, left_len);

        str->offset = new_offset;
        str->buff[new_offset] = 0;
    }

    return 0;
}