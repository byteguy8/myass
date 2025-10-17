#ifndef MYASS_H
#define MYASS_H

#include "types.h"
#include "memory.h"
#include <stdint.h>

typedef struct myass MyAss;

MyAss *myass_create(const Allocator *allocator);
void myass_destroy(MyAss *myass);

void myass_print_as_hex(const MyAss *myass, int wprefix);

void myass_mov_r64_imm32(X64Register dst, dword src, MyAss *myass);
void myass_mov_r64_r64(X64Register dst, X64Register src, MyAss *myass);

void myass_add_r64_r64(X64Register dst, X64Register src, MyAss *myass);
void myass_add_r64_imm32(X64Register dst, dword src, MyAss *myass);

void myass_cmp_r64_r64(X64Register dst, X64Register src, MyAss *myass);
void myass_cmp_r64_imm32(X64Register dst, dword src, MyAss *myass);

void myass_idiv_r64(X64Register src, MyAss *myass);
void myass_imul_r64_r64(X64Register dst, X64Register src, MyAss *myass);

void myass_je_imm32(dword offset, MyAss *myass);
void myass_jg_imm32(dword offset, MyAss *myass);
void myass_jl_imm32(dword offset, MyAss *myass);
void myass_jge_imm32(dword offset, MyAss *myass);
void myass_jle_imm32(dword offset, MyAss *myass);
void myass_jmp_imm32(dword offset, MyAss *myass);

void myass_sub_r64_r64(X64Register dst, X64Register src, MyAss *myass);
void myass_sub_r64_imm32(X64Register dst, dword src, MyAss *myass);

void myass_ret(MyAss *myass);

void myass_xor_r64_r64(X64Register dst, X64Register src, MyAss *myass);
void myass_xor_r64_imm32(X64Register dst, dword src, MyAss *myass);

int myass_assemble(MyAss *myass, size_t input_len, const char *input);

#endif
