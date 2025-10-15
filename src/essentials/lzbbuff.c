#include "lzbbuff.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>

static void *lzalloc(const LZBBuffAllocator *allocator, size_t size);
static void *lzrealloc(const LZBBuffAllocator *allocator, void *ptr, size_t old_size, size_t new_size);
static void lzdealloc(const LZBBuffAllocator *allocator, void *ptr, size_t size);

#define MEMORY_ALLOC(_allocator, _type, _count)                         ((_type *)lzalloc((_allocator), sizeof(_type) * (_count)))
#define MEMORY_REALLOC(_allocator, _ptr, _type, _old_count, _new_count) ((_type *)(lzrealloc((_allocator), (_ptr), sizeof(_type) * (_old_count), sizeof(_type) * (_new_count))))
#define MEMORY_DEALLOC(_allocator, _ptr, _type, _count)                 (lzdealloc((_allocator), (_ptr), sizeof(_type) * (_count)))

static lzbbuff_hash fnv_1a_hash(size_t key_size, const uint8_t *key);
static lzbbuff_byte *align_ptr(size_t alignment, lzbbuff_byte *ptr);
static void print_byte_as_hex(lzbbuff_byte byte);
static int grow(size_t extra, LZBBuff *buff);

inline void *lzalloc(const LZBBuffAllocator *allocator, size_t size){
    return allocator ? allocator->alloc(size, allocator->ctx) : malloc(size);
}

inline void *lzrealloc(const LZBBuffAllocator *allocator, void *ptr, size_t old_size, size_t new_size){
    return allocator ? allocator->realloc(ptr, old_size, new_size, allocator->ctx) : realloc(ptr, new_size);
}

inline void lzdealloc(const LZBBuffAllocator *allocator, void *ptr, size_t size){
    if(allocator){
        allocator->dealloc(ptr, size, allocator->ctx);
    }else{
        free(ptr);
    }
}

inline lzbbuff_hash fnv_1a_hash(size_t key_size, const uint8_t *key){
    const uint64_t prime = 0x00000100000001b3;
    const uint64_t basis = 0xcbf29ce484222325;
    uint64_t hash = basis;

    for (size_t i = 0; i < key_size; i++){
        hash ^= key[i];
        hash *= prime;
    }

    return hash;
}

inline lzbbuff_byte *align_ptr(size_t alignment, lzbbuff_byte *ptr){
    uintptr_t iptr = (uintptr_t)ptr;
    size_t mod = iptr % alignment;
    size_t padd = mod == 0 ? 0 : alignment - mod;

    return (lzbbuff_byte *)(iptr + padd);
}

inline void print_byte_as_hex(lzbbuff_byte byte){
    switch (byte){
        case 10:{
            printf("a");
            break;
        }case 11:{
            printf("b");
            break;
        }case 12:{
            printf("c");
            break;
        }case 13:{
            printf("d");
            break;
        }case 14:{
            printf("e");
            break;
        }case 15:{
            printf("f");
            break;
        }default:{
            printf("%" PRIu8, byte);
        }
    }
}

int grow(size_t extra, LZBBuff *buff){
    size_t used_bytes = lzbbuff_used_bytes(buff);

    size_t old_capacity = buff->capacity;
    size_t new_capacity = (old_capacity + extra) * 2;
    void *old_raw_buff = buff->raw_buff;

    void *new_raw_buff = MEMORY_REALLOC(
        buff->allocator,
        old_raw_buff,
        lzbbuff_byte,
        old_capacity,
        new_capacity
    );

    if(!new_raw_buff){
        return 1;
    }

    buff->capacity = new_capacity;
    buff->offset = new_raw_buff + used_bytes;
    buff->raw_buff = new_raw_buff;

    return 0;
}

LZBBuff *lzbbuff_create(size_t len, LZBBuffAllocator *allocator){
    lzbbuff_byte *raw_buff = MEMORY_ALLOC(allocator, lzbbuff_byte, len);
    LZBBuff *buff = MEMORY_ALLOC(allocator, LZBBuff, 1);

    if(!raw_buff || !buff){
        MEMORY_DEALLOC(allocator, raw_buff, lzbbuff_byte, len);
        MEMORY_DEALLOC(allocator, buff, LZBBuff, 1);

        return NULL;
    }

    buff->capacity = len;
    buff->offset = raw_buff;
    buff->raw_buff = raw_buff;
    buff->allocator = allocator;

    return buff;
}

void lzbbuff_destroy(LZBBuff *buff){
    if(!buff){
        return;
    }

    LZBBuffAllocator *allocator = buff->allocator;

    MEMORY_DEALLOC(allocator, buff->raw_buff, lzbbuff_byte, buff->capacity);
    MEMORY_DEALLOC(allocator, buff, LZBBuff, 1);
}

inline int lzbbuff_restart(LZBBuff *buff){
    size_t used_bytes = lzbbuff_used_bytes(buff);

    buff->offset = buff->raw_buff;

    return used_bytes;
}

inline size_t lzbbuff_used_bytes(const LZBBuff *buff){
    return buff->offset - buff->raw_buff;
}

void lzbbuff_print_as_hex(const LZBBuff *buff, int wprefix){
    size_t len = lzbbuff_used_bytes(buff);
    lzbbuff_byte *raw_buff = buff->raw_buff;

    if(len > 0 && wprefix){
        printf("0x");
    }

    for (size_t i = 0; i < len; i++){
        lzbbuff_byte byte = raw_buff[i];
        lzbbuff_byte high = (byte >> 4) & 0xF;
        lzbbuff_byte low = byte & 0xF;

        print_byte_as_hex(high);
        print_byte_as_hex(low);
    }

    if(len > 0){
        printf("\n");
    }
}

lzbbuff_hash lzbbuff_hash_bytes(const LZBBuff *buff){
    size_t used_bytes = lzbbuff_used_bytes(buff);

    if(used_bytes == 0){
        return 0;
    }

    return fnv_1a_hash(used_bytes, buff->raw_buff);
}

void *lzbbuff_copy_raw_buff(const LZBBuff *buff, const LZBBuffAllocator *allocator, size_t *out_len){
    size_t used_bytes = lzbbuff_used_bytes(buff);

    if(used_bytes == 0){
        return NULL;
    }

    void *copy_raw_buff = MEMORY_ALLOC(allocator, lzbbuff_byte, used_bytes);

    memcpy(copy_raw_buff, buff->raw_buff, used_bytes);

    if(out_len){
        *out_len = used_bytes;
    }

    return copy_raw_buff;
}

int lzbbuff_write_bytes(LZBBuff *buff, size_t alignment, size_t len, const void *bytes){
    lzbbuff_byte *new_offset = (alignment > 0 ? align_ptr(alignment, buff->offset) : buff->offset) + len;

    if((new_offset >= (buff->raw_buff + buff->capacity))){
        if(grow(len, buff)){
            return 1;
        }

        new_offset = (alignment > 0 ? align_ptr(alignment, buff->offset) : buff->offset) + len;
    }

    memmove(new_offset - len, bytes, len);

    buff->offset = new_offset;

    return 0;
}

inline int lzbbuff_write_byte(LZBBuff *buff, size_t alignment, lzbbuff_byte value){
    return lzbbuff_write_bytes(buff, alignment, sizeof(lzbbuff_byte), &value);
}

inline int lzbbuff_write_word(LZBBuff *buff, size_t alignment, lzbbuff_word value){
    return lzbbuff_write_bytes(buff, alignment, sizeof(lzbbuff_word), &value);
}

inline int lzbbuff_write_dword(LZBBuff *buff, size_t alignment, lzbbuff_dword value){
    return lzbbuff_write_bytes(buff, alignment, sizeof(lzbbuff_dword), &value);
}

inline int lzbbuff_write_qword(LZBBuff *buff, size_t alignment, lzbbuff_qword value){
    return lzbbuff_write_bytes(buff, alignment, sizeof(lzbbuff_qword), &value);
}

int lzbbuff_write_ascii(LZBBuff *buff, size_t alignment, lzbbuff_ascii value){
    return lzbbuff_write_bytes(buff, alignment, strlen(value), value);
}