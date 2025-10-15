#include "myass.h"
#include "essentials/lzohtable.h"
#include "essentials/lzarena.h"
#include "essentials/lzbbuff.h"
#include "essentials/memory.h"

#include "token.h"
#include "lexer.h"
#include "parser.h"

#include "location.h"
#include "instruction.h"

#include <assert.h>
#include <stdio.h>
#include <setjmp.h>

#define ARENA (myass->arena)
#define ALLOCATOR (&(myass->arena_allocator))
#define BBUFF (myass->bbuff)

typedef struct myass{
    jmp_buf          err_buf;
    LZBBuff          *bbuff;
    LZArena          *arena;
    AllocatorContext *arena_allocator_context;
    Allocator        arena_allocator;
    const Allocator  *allocator;
}MyAss;

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

static void add_keyword(LZOHTable *keywords, const char *name, TokenType type){
    lzohtable_put_ckv(
        strlen(name),
        name,
        sizeof(TokenType),
        &type,
        keywords,
        NULL
    );
}

LZOHTable *create_registers_keywords(Allocator *allocator){
    LZOHTable *registers = MEMORY_LZOHTABLE(allocator);

    add_keyword(registers, "rax", REGISTER_TOKEN_TYPE);
    add_keyword(registers, "rcx", REGISTER_TOKEN_TYPE);
    add_keyword(registers, "rdx", REGISTER_TOKEN_TYPE);
    add_keyword(registers, "rbx", REGISTER_TOKEN_TYPE);
    add_keyword(registers, "rsp", REGISTER_TOKEN_TYPE);
    add_keyword(registers, "rbp", REGISTER_TOKEN_TYPE);
    add_keyword(registers, "rsi", REGISTER_TOKEN_TYPE);
    add_keyword(registers, "rdi", REGISTER_TOKEN_TYPE);
    add_keyword(registers, "r8", REGISTER_TOKEN_TYPE);
    add_keyword(registers, "r9", REGISTER_TOKEN_TYPE);
    add_keyword(registers, "r10", REGISTER_TOKEN_TYPE);
    add_keyword(registers, "r11", REGISTER_TOKEN_TYPE);
    add_keyword(registers, "r12", REGISTER_TOKEN_TYPE);
    add_keyword(registers, "r13", REGISTER_TOKEN_TYPE);
    add_keyword(registers, "r14", REGISTER_TOKEN_TYPE);
    add_keyword(registers, "r15", REGISTER_TOKEN_TYPE);

    return registers;
}

LZOHTable *create_instructions_keywords(Allocator *allocator){
    LZOHTable *registers = MEMORY_LZOHTABLE(allocator);

    add_keyword(registers, "add", ADD_TOKEN_TYPE);
    add_keyword(registers, "idiv", IDIV_TOKEN_TYPE);
    add_keyword(registers, "mov", MOV_TOKEN_TYPE);
    add_keyword(registers, "imul", IMUL_TOKEN_TYPE);
    add_keyword(registers, "sub", SUB_TOKEN_TYPE);
    add_keyword(registers, "ret", RET_TOKEN_TYPE);
    add_keyword(registers, "xor", XOR_TOKEN_TYPE);

    return registers;
}

static void assemble_add_instruction(MyAss *myass, BinaryInstruction *instruction){
    Location *dst_location = instruction->dst_location;
    Location *src_location = instruction->src_location;

    switch (dst_location->type){
        case REGISTER_LOCATION_TYPE:{
            RegisterLocation *dst = (RegisterLocation *)dst_location->sub_location;

            switch (src_location->type){
                case LITERAL_LOCATION_TYPE:{
                    LiteralLocation *src = (LiteralLocation *)src_location->sub_location;

                    myass_add_r64_imm32(dst->reg, src->value, myass);

                    break;
                }case REGISTER_LOCATION_TYPE:{
                    RegisterLocation *src = (RegisterLocation *)src_location->sub_location;

                    myass_add_r64_r64(dst->reg, src->reg, myass);

                    break;
                }default:{
                    assert(0 && "Illegal location type");
                }
            }

            break;
        }default:{
            assert(0 && "Illegal location type");
        }
    }
}

static void assemble_idiv_instruction(MyAss *myass, UnaryInstruction *instruction){
    Location *src_location = instruction->src_location;

    switch (src_location->type){
        case REGISTER_LOCATION_TYPE:{
            switch (src_location->type){
                case REGISTER_LOCATION_TYPE:{
                    RegisterLocation *src = (RegisterLocation *)src_location->sub_location;

                    myass_idiv_r64(src->reg, myass);

                    break;
                }default:{
                    assert(0 && "Illegal location type");
                }
            }

            break;
        }default:{
            assert(0 && "Illegal location type");
        }
    }
}

static void assemble_mov_instruction(MyAss *myass, BinaryInstruction *instruction){
    Location *dst_location = instruction->dst_location;
    Location *src_location = instruction->src_location;

    switch (dst_location->type){
        case REGISTER_LOCATION_TYPE:{
            RegisterLocation *dst = (RegisterLocation *)dst_location->sub_location;

            switch (src_location->type){
                case LITERAL_LOCATION_TYPE:{
                    LiteralLocation *src = (LiteralLocation *)src_location->sub_location;

                    myass_mov_r64_imm32(dst->reg, src->value, myass);

                    break;
                }case REGISTER_LOCATION_TYPE:{
                    RegisterLocation *src = (RegisterLocation *)src_location->sub_location;

                    myass_mov_r64_r64(dst->reg, src->reg, myass);

                    break;
                }default:{
                    assert(0 && "Illegal location type");
                }
            }

            break;
        }default:{
            assert(0 && "Illegal location type");
        }
    }
}

static void assemble_imul_instruction(MyAss *myass, BinaryInstruction *instruction){
    Location *dst_location = instruction->dst_location;
    Location *src_location = instruction->src_location;

    switch (dst_location->type){
        case REGISTER_LOCATION_TYPE:{
            RegisterLocation *dst = (RegisterLocation *)dst_location->sub_location;

            switch (src_location->type){
                case REGISTER_LOCATION_TYPE:{
                    RegisterLocation *src = (RegisterLocation *)src_location->sub_location;

                    myass_imul_r64_r64(dst->reg, src->reg, myass);

                    break;
                }default:{
                    assert(0 && "Illegal location type");
                }
            }

            break;
        }default:{
            assert(0 && "Illegal location type");
        }
    }
}

static void assemble_sub_instruction(MyAss *myass, BinaryInstruction *instruction){
    Location *dst_location = instruction->dst_location;
    Location *src_location = instruction->src_location;

    switch (dst_location->type){
        case REGISTER_LOCATION_TYPE:{
            RegisterLocation *dst = (RegisterLocation *)dst_location->sub_location;

            switch (src_location->type){
                case LITERAL_LOCATION_TYPE:{
                    LiteralLocation *src = (LiteralLocation *)src_location->sub_location;

                    myass_sub_r64_imm32(dst->reg, src->value, myass);

                    break;
                }case REGISTER_LOCATION_TYPE:{
                    RegisterLocation *src = (RegisterLocation *)src_location->sub_location;

                    myass_sub_r64_r64(dst->reg, src->reg, myass);

                    break;
                }default:{
                    assert(0 && "Illegal location type");
                }
            }

            break;
        }default:{
            assert(0 && "Illegal location type");
        }
    }
}

static void assemble_ret_instruction(MyAss *myass){
    myass_ret(myass);
}

static void assemble_xor_instruction(MyAss *myass, BinaryInstruction *instruction){
    Location *dst_location = instruction->dst_location;
    Location *src_location = instruction->src_location;

    switch (dst_location->type){
        case REGISTER_LOCATION_TYPE:{
            RegisterLocation *dst = (RegisterLocation *)dst_location->sub_location;

            switch (src_location->type){
                case LITERAL_LOCATION_TYPE:{
                    LiteralLocation *src = (LiteralLocation *)src_location->sub_location;

                    myass_xor_r64_imm32(dst->reg, src->value, myass);

                    break;
                }case REGISTER_LOCATION_TYPE:{
                    RegisterLocation *src = (RegisterLocation *)src_location->sub_location;

                    myass_xor_r64_r64(dst->reg, src->reg, myass);

                    break;
                }default:{
                    assert(0 && "Illegal location type");
                }
            }

            break;
        }default:{
            assert(0 && "Illegal location type");
        }
    }
}

static void assemble_instruction(MyAss *myass, Instruction *instruction){
    switch (instruction->type){
        case ADD_INSTRUCTION_TYPE:{
            assemble_add_instruction(myass, (BinaryInstruction *)instruction->sub_instruction);

            break;
        }case IDIV_INSTRUCTION_TYPE:{
            assemble_idiv_instruction(myass, (UnaryInstruction *)instruction->sub_instruction);

            break;
        }case MOV_INSTRUCTION_TYPE:{
            assemble_mov_instruction(myass, (BinaryInstruction *)instruction->sub_instruction);

            break;
        }case IMUL_INSTRUCTION_TYPE:{
            assemble_imul_instruction(myass, (BinaryInstruction *)instruction->sub_instruction);

            break;
        }case SUB_INSTRUCTION_TYPE:{
            assemble_sub_instruction(myass, (BinaryInstruction *)instruction->sub_instruction);

            break;
        }case RET_INSTRUCTION_TYPE:{
            assemble_ret_instruction(myass);

            break;
        }case XOR_INSTRUCTION_TYPE:{
            assemble_xor_instruction(myass, (BinaryInstruction *)instruction->sub_instruction);

            break;
        }
    }
}

static void assemble_instructions(MyAss *myass, DynArr *instructions){
    size_t len = DYNARR_LEN(instructions);

    for (size_t i = 0; i < len; i++){
        Instruction *instruction = DYNARR_GET_PTR_AS(Instruction, i, instructions);

        assemble_instruction(myass, instruction);
    }
}

MyAss *myass_create(const Allocator *allocator){
    LZBBuff *bbuff = lzbbuff_create(8192, (LZBBuffAllocator *)allocator);
    LZArena *arena = lzarena_create((LZArenaAllocator *)allocator);
    AllocatorContext *allocator_context = MEMORY_ALLOC(AllocatorContext, 1, allocator);
    MyAss *myass = MEMORY_ALLOC(MyAss, 1, allocator);

    if(!bbuff || !arena || !allocator_context || !myass){
        lzbbuff_destroy(bbuff);
        lzarena_destroy(arena);
        MEMORY_DEALLOC(allocator_context, AllocatorContext, 1, allocator);
        MEMORY_DEALLOC(myass, MyAss, 1, allocator);

        return NULL;
    }

    allocator_context->err_buf = &myass->err_buf;
    allocator_context->behind_allocator = arena;

    myass->bbuff = bbuff;
    myass->arena = arena;
    myass->arena_allocator_context = allocator_context;
    myass->allocator = allocator;

    MEMORY_INIT_ALLOCATOR(
        allocator_context,
        memory_arena_alloc,
        memory_arena_realloc,
        memory_arena_dealloc,
        &myass->arena_allocator
    );

    return myass;
}

void myass_destroy(MyAss *myass){
    if(!myass){
        return;
    }

    const Allocator *allocator = myass->allocator;

    lzbbuff_destroy(myass->bbuff);
    MEMORY_DEALLOC(myass->arena_allocator_context, AllocatorContext, 1, allocator);
    lzarena_destroy(myass->arena);
    MEMORY_DEALLOC(myass, MyAss, 1, allocator);
}

void myass_print_as_hex(const MyAss *myass, int wprefix){
    LZBBuff *bbuff = BBUFF;

    lzbbuff_print_as_hex(bbuff, wprefix);
}

void myass_mov_r64_imm32(X64Register dst, qword src, MyAss *myass){
    LZBBuff *bbuff = BBUFF;

    lzbbuff_write_byte(bbuff, 0, rex(1, dst > 7, 0, 0));
    lzbbuff_write_byte(bbuff, 0, 0xb8 | (((byte)dst) & 0x07));
    lzbbuff_write_qword(bbuff, 0, src);
}

void myass_mov_r64_r64(X64Register dst, X64Register src, MyAss *myass){
    LZBBuff *bbuff = BBUFF;

    lzbbuff_write_byte(bbuff, 0, rex(1, dst > 7, 0, src > 7));
    lzbbuff_write_byte(bbuff, 0, 0x8b);
    lzbbuff_write_byte(bbuff, 0, mod_rm(REG_MODE, dst, src));
}

void myass_add_r64_r64(X64Register dst, X64Register src, MyAss *myass){
    LZBBuff *bbuff = BBUFF;

    lzbbuff_write_byte(bbuff, 0, rex(1, dst > 7, 0, src > 7));
    lzbbuff_write_byte(bbuff, 0, 0x03);
    lzbbuff_write_byte(bbuff, 0, mod_rm(REG_MODE, dst, src));
}

void myass_add_r64_imm32(X64Register dst, dword src, MyAss *myass){
    LZBBuff *bbuff = BBUFF;

    lzbbuff_write_byte(bbuff, 0, rex(1, 0, 0, dst > 7));
    lzbbuff_write_byte(bbuff, 0, 0x81);
    lzbbuff_write_byte(bbuff, 0, mod_rm(REG_MODE, 0, dst));
    lzbbuff_write_dword(bbuff, 0, src);
}

void myass_idiv_r64(X64Register src, MyAss *myass){
    LZBBuff *bbuff = BBUFF;

    lzbbuff_write_byte(bbuff, 0, rex(1, 0, 0, src > 7));
    lzbbuff_write_byte(bbuff, 0, 0xf7);
    lzbbuff_write_byte(bbuff, 0, mod_rm(REG_MODE, 7, src));
}

void myass_imul_r64_imm32(X64Register dst, dword src, MyAss *myass){
    LZBBuff *bbuff = BBUFF;

    lzbbuff_write_byte(bbuff, 0, rex(1, dst > 7, 0, src > 7));
    lzbbuff_write_byte(bbuff, 0, 0x0f);
    lzbbuff_write_byte(bbuff, 0, 0xaf);
    lzbbuff_write_byte(bbuff, 0, mod_rm(REG_MODE, dst, src));
}

void myass_imul_r64_r64(X64Register dst, X64Register src, MyAss *myass){
    LZBBuff *bbuff = BBUFF;

    lzbbuff_write_byte(bbuff, 0, rex(1, dst > 7, 0, src > 7));
    lzbbuff_write_byte(bbuff, 0, 0x0f);
    lzbbuff_write_byte(bbuff, 0, 0xaf);
    lzbbuff_write_byte(bbuff, 0, mod_rm(REG_MODE, dst, src));
}

void myass_sub_r64_r64(X64Register dst, X64Register src, MyAss *myass){
    LZBBuff *bbuff = BBUFF;

    lzbbuff_write_byte(bbuff, 0, rex(1, dst > 7, 0, src > 7));
    lzbbuff_write_byte(bbuff, 0, 0x2b);
    lzbbuff_write_byte(bbuff, 0, mod_rm(REG_MODE, dst, src));
}

void myass_sub_r64_imm32(X64Register dst, dword src, MyAss *myass){
    LZBBuff *bbuff = BBUFF;

    lzbbuff_write_byte(bbuff, 0, rex(1, 0, 0, dst > 7));
    lzbbuff_write_byte(bbuff, 0, 0x81);
    lzbbuff_write_byte(bbuff, 0, mod_rm(REG_MODE, 5, dst));
    lzbbuff_write_dword(bbuff, 0, src);
}

void myass_ret(MyAss *myass){
    LZBBuff *bbuff = BBUFF;

    lzbbuff_write_byte(bbuff, 0, 0xc3);
}

void myass_xor_r64_r64(X64Register dst, X64Register src, MyAss *myass){
    LZBBuff *bbuff = BBUFF;

    lzbbuff_write_byte(bbuff, 0, rex(1, dst > 7, 0, src > 7));
    lzbbuff_write_byte(bbuff, 0, 0x33);
    lzbbuff_write_byte(bbuff, 0, mod_rm(REG_MODE, dst, src));
}

void myass_xor_r64_imm32(X64Register dst, dword src, MyAss *myass){
    LZBBuff *bbuff = BBUFF;

    lzbbuff_write_byte(bbuff, 0, rex(1, 0, 0, dst > 7));
    lzbbuff_write_byte(bbuff, 0, 0x81);
    lzbbuff_write_byte(bbuff, 0, mod_rm(REG_MODE, 6, dst));
    lzbbuff_write_dword(bbuff, 0, src);
}

int myass_assemble(MyAss *myass, size_t input_len, const char *input){
    if(setjmp(myass->err_buf) == 0){
        lzbbuff_restart(BBUFF);

        LZOHTable *registers_keywords = create_registers_keywords(ALLOCATOR);
        LZOHTable *instructions_keywords = create_instructions_keywords(ALLOCATOR);
        DynArr *tokens = MEMORY_DYNARR_PTR(ALLOCATOR);
        DynArr *instructions = MEMORY_DYNARR_PTR(ALLOCATOR);
        BStr code = {.len = input_len, .buff = input};
        Lexer *lexer = lexer_create(ALLOCATOR);
        Parser *parser = parser_create(ALLOCATOR);

        if(lexer_lex(lexer, registers_keywords, instructions_keywords, &code, tokens)){
            return 1;
        }

        if(parser_parse(parser, tokens, instructions)){
            return 1;
        }

        assemble_instructions(myass, instructions);
        lzarena_free_all(ARENA);

        return 0;
    }else{
        return 1;
    }
}