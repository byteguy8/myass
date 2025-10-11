#ifndef MYASS_H
#define MYASS_H

#include "memory.h"
#include <stdint.h>

typedef uint8_t  byte;
typedef uint16_t word;
typedef uint32_t dword;
typedef uint64_t qword;

typedef enum x64_register{
    RAX,
    RCX,
    RDX,
    RBX,
    RSP,
    RBP,
    RSI,
    RDI,
    R8,
    R9,
    R10,
    R11,
    R12,
    R13,
    R14,
    R15
}X64Register;

typedef struct myass MyAss;

MyAss *myass_create(Allocator *allocator);

void myass_destroy(MyAss *myass);

void myass_print_as_hex(MyAss *myass);

void myass_mov_r64_imm64(X64Register dst, qword src, MyAss *myass);
void myass_mov_r64_r64(X64Register dst, X64Register src, MyAss *myass);

void myass_add_r64_r64(X64Register dst, X64Register src, MyAss *myass);
void myass_add_r64_imm32(X64Register dst, dword src, MyAss *myass);

void myass_idiv_r64_r64(X64Register src, MyAss *myass);
void myass_imul_r64_r64(X64Register dst, X64Register src, MyAss *myass);

void myass_sub_r64_r64(X64Register dst, X64Register src, MyAss *myass);
void myass_sub_r64_imm32(X64Register dst, dword src, MyAss *myass);

void myass_ret(MyAss *myass);

void myass_xor_r64_r64(X64Register dst, X64Register src, MyAss *myass);
void myass_xor_r64_imm32(X64Register dst, dword src, MyAss *myass);

#endif