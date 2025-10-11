#ifndef LZBBUFF
#define LZBBUFF

#include <stddef.h>
#include <stdint.h>

typedef struct lzbbuff_allocator{
    void *ctx;
    void *(*alloc)(size_t size, void *ctx);
    void *(*realloc)(void *ptr, size_t old_size, size_t new_size, void *ctx);
    void (*dealloc)(void *ptr, size_t size, void *ctx);
}LZBBuffAllocator;

typedef uint8_t       lzbbuff_byte;
typedef uint16_t      lzbbuff_word;
typedef uint32_t      lzbbuff_dword;
typedef uint64_t      lzbbuff_qword;
typedef char *        lzbbuff_ascii;
typedef uint64_t      lzbbuff_hash;

typedef struct lzbbuff{
    size_t capacity;
    lzbbuff_byte *offset;
    lzbbuff_byte *raw_buff;
    LZBBuffAllocator *allocator;
}LZBBuff;

LZBBuff *lzbbuff_create(size_t len, LZBBuffAllocator *allocator);

void lzbbuff_destroy(LZBBuff *buff);

size_t lzbbuff_used_bytes(const LZBBuff *buff);

lzbbuff_hash lzbbuff_hash_bytes(const LZBBuff *buff);

void lzbbuff_print_as_hex(const LZBBuff *buff);

void *lzbbuff_offset(const LZBBuff *buff);

int lzbbuff_write_bytes(LZBBuff *buff, size_t alignment, size_t len, const void *bytes);

int lzbbuff_write_byte(LZBBuff *buff, size_t alignment, lzbbuff_byte value);

int lzbbuff_write_word(LZBBuff *buff, size_t alignment, lzbbuff_word value);

int lzbbuff_write_dword(LZBBuff *buff, size_t alignment, lzbbuff_dword value);

int lzbbuff_write_qword(LZBBuff *buff, size_t alignment, lzbbuff_qword value);

int lzbbuff_write_ascii(LZBBuff *buff, size_t alignment, const lzbbuff_ascii value);

#define LZBBUFF_WRITE_BYTE(_buff, _value)  (lzbbuff_write_byte(_buff, sizeof(lzbbuff_byte), _value))

#define LZBBUFF_WRITE_WORD(_buff, _value)  (lzbbuff_write_word(_buff, sizeof(lzbbuff_word), _value))

#define LZBBUFF_WRITE_DWORD(_buff, _value) (lzbbuff_write_dword(_buff, sizeof(lzbbuff_dword), _value))

#define LZBBUFF_WRITE_QWORD(_buff, _value) (lzbbuff_write_qword(_buff, sizeof(lzbbuff_qword), _value))

#define LZBBUFF_WRITE_ASCII(_buff, _value) (lzbbuff_write_ascii(_buff, 0, _value))

void *lzbbuff_copy_raw_buff(const LZBBuff *buff, const LZBBuffAllocator *allocator, size_t *out_len);

int lzbbuff_restart(LZBBuff *buff);

#endif