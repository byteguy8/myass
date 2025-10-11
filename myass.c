#include "myass.h"
#include "lzbbuff.h"

#include <assert.h>
#include <stdio.h>

typedef enum mod{
    MEM_MODE_NO_DISPLACEMENT,
    MEM_MODE_8BIT_DISPLACEMENT,
    MEM_MODE_32BIT_DISPLACEMENT,
    REG_MODE,
}Mod;

static inline byte rex(
    byte w, // 64bit mode
    byte r, // extend reg field (ModRM)
    byte x, // extend index field (SIB)
    byte b  // extend r/m base field
){
    // 0100WRXB
    assert(w == 0 || w == 1);
    assert(r == 0 || r == 1);
    assert(x == 0 || x == 1);
    assert(b == 0 || b == 1);

    return 0b01000000 | (w << 3) | (r << 2) | (x << 1) | b;
}

static inline byte mod_rm(Mod mod, X64Register dest, X64Register source){
    return (((byte)(mod & 0x3)) << 6) | (((byte)(dest & 0x7)) << 3) | (source & 0x7);
}

MyAss *myass_create(Allocator *allocator){
    LZBBuff *bbuff = lzbbuff_create(8192, (LZBBuffAllocator *)allocator);

    if(!bbuff){
        return NULL;
    }

    return (MyAss *)bbuff;
}

void myass_destroy(MyAss *myass){
    lzbbuff_destroy((LZBBuff *)myass);
}

void myass_print_as_hex(MyAss *myass){
    LZBBuff *bbuff = (LZBBuff *)myass;

    lzbbuff_print_as_hex(bbuff);
}

void myass_mov_r64_imm64(X64Register dst, qword src, MyAss *myass){
    LZBBuff *bbuff = (LZBBuff *)myass;

    lzbbuff_write_byte(bbuff, 0, rex(1, dst > 7, 0, 0));
    lzbbuff_write_byte(bbuff, 0, 0xb8 | (((byte)dst) & 0x07));
    lzbbuff_write_qword(bbuff, 0, src);
}

void myass_mov_r64_r64(X64Register dst, X64Register src, MyAss *myass){
    LZBBuff *bbuff = (LZBBuff *)myass;

    lzbbuff_write_byte(bbuff, 0, rex(1, dst > 7, 0, src > 7));
    lzbbuff_write_byte(bbuff, 0, 0x8b);
    lzbbuff_write_byte(bbuff, 0, mod_rm(REG_MODE, dst, src));
}

void myass_add_r64_r64(X64Register dst, X64Register src, MyAss *myass){
    LZBBuff *bbuff = (LZBBuff *)myass;

    lzbbuff_write_byte(bbuff, 0, rex(1, dst > 7, 0, src > 7));
    lzbbuff_write_byte(bbuff, 0, 0x03);
    lzbbuff_write_byte(bbuff, 0, mod_rm(REG_MODE, dst, src));
}

void myass_add_r64_imm32(X64Register dst, dword src, MyAss *myass){
    LZBBuff *bbuff = (LZBBuff *)myass;

    lzbbuff_write_byte(bbuff, 0, rex(1, 0, 0, dst > 7));
    lzbbuff_write_byte(bbuff, 0, 0x81);
    lzbbuff_write_byte(bbuff, 0, mod_rm(REG_MODE, 0, dst));
    lzbbuff_write_dword(bbuff, 0, src);
}

void myass_idiv_r64_r64(X64Register src, MyAss *myass){
    LZBBuff *bbuff = (LZBBuff *)myass;

    lzbbuff_write_byte(bbuff, 0, rex(1, 0, 0, src > 7));
    lzbbuff_write_byte(bbuff, 0, 0xf7);
    lzbbuff_write_byte(bbuff, 0, mod_rm(REG_MODE, 7, src));
}

void myass_imul_r64_r64(X64Register dst, X64Register src, MyAss *myass){
    LZBBuff *bbuff = (LZBBuff *)myass;

    lzbbuff_write_byte(bbuff, 0, rex(1, dst > 7, 0, src > 7));
    lzbbuff_write_byte(bbuff, 0, 0x0f);
    lzbbuff_write_byte(bbuff, 0, 0xaf);
    lzbbuff_write_byte(bbuff, 0, mod_rm(REG_MODE, dst, src));
}

void myass_sub_r64_r64(X64Register dst, X64Register src, MyAss *myass){
    LZBBuff *bbuff = (LZBBuff *)myass;

    lzbbuff_write_byte(bbuff, 0, rex(1, dst > 7, 0, src > 7));
    lzbbuff_write_byte(bbuff, 0, 0x2b);
    lzbbuff_write_byte(bbuff, 0, mod_rm(REG_MODE, dst, src));
}

void myass_sub_r64_imm32(X64Register dst, dword src, MyAss *myass){
    LZBBuff *bbuff = (LZBBuff *)myass;

    lzbbuff_write_byte(bbuff, 0, rex(1, 0, 0, dst > 7));
    lzbbuff_write_byte(bbuff, 0, 0x81);
    lzbbuff_write_byte(bbuff, 0, mod_rm(REG_MODE, 5, dst));
    lzbbuff_write_dword(bbuff, 0, src);
}

void myass_ret(MyAss *myass){
    LZBBuff *bbuff = (LZBBuff *)myass;

    lzbbuff_write_byte(bbuff, 0, 0xc3);
}

void myass_xor_r64_r64(X64Register dst, X64Register src, MyAss *myass){
    LZBBuff *bbuff = (LZBBuff *)myass;

    lzbbuff_write_byte(bbuff, 0, rex(1, dst > 7, 0, src > 7));
    lzbbuff_write_byte(bbuff, 0, 0x33);
    lzbbuff_write_byte(bbuff, 0, mod_rm(REG_MODE, dst, src));
}

void myass_xor_r64_imm32(X64Register dst, dword src, MyAss *myass){
    LZBBuff *bbuff = (LZBBuff *)myass;

    lzbbuff_write_byte(bbuff, 0, rex(1, 0, 0, dst > 7));
    lzbbuff_write_byte(bbuff, 0, 0x81);
    lzbbuff_write_byte(bbuff, 0, mod_rm(REG_MODE, 6, dst));
    lzbbuff_write_dword(bbuff, 0, src);
}