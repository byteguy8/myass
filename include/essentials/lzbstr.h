#ifndef LZBSTR_H
#define LZBSTR_H

#include <stddef.h>

typedef struct lzbstr_allocator{
    void *ctx;
    void *(*alloc)(size_t size, void *ctx);
    void *(*realloc)(void *ptr, size_t old_size, size_t new_size, void *ctx);
    void (*dealloc)(void *ptr, size_t size, void *ctx);
}LZBStrAllocator;

typedef struct lzbstr{
    size_t offset;
    size_t buff_len;
    char *buff;
    LZBStrAllocator *allocator;
}LZBStr;

LZBStr *lzbstr_create(LZBStrAllocator *allocator);

void lzbstr_destroy(LZBStr *str);

char *lzbstr_destroy_save_buff(LZBStr *str);

#define LZBSTR_LEN(_str)((_str)->offset)

size_t lzbstr_available_space(LZBStr *str);

void lzbstr_reset(LZBStr *str);

int lzbstr_grow_by(size_t by, LZBStr *str);

char *lzbstr_rclone_buff_rng(size_t from, size_t to, LZBStrAllocator *allocator, LZBStr *str, size_t *out_len);

#define LZBSTR_CLONE_BUFF_RNG(_from, _to, _str, _out_len)(lzbstr_rclone_buff_rng(_from, _to, NULL, _str, _out_len))

char *lzbstr_rclone_buff(LZBStrAllocator *allocator, LZBStr *str, size_t *out_len);

#define LZBSTR_CLONE_BUFF(_str, _out_len)(lzbstr_rclone_buff(NULL, _str, _out_len))

int lzbstr_append(char *rstr, LZBStr *str);

int lzbstr_append_args(LZBStr *str, char *fmt, ...);

int lzbstr_insert_args(LZBStr *str, size_t from, char *fmt, ...);

int lzbstr_remove(size_t from, size_t to, LZBStr *str);

#endif