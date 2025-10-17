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
#include <stddef.h>
#include <stdio.h>
#include <setjmp.h>
#include <stdarg.h>
#include <inttypes.h>

#define ARENA (myass->arena)
#define ALLOCATOR (&(myass->arena_allocator))
#define BBUFF (myass->bbuff)

typedef enum symbol_type{
    LABEL_SYMBOL_TYPE,
}SymbolType;

typedef struct symbol{
    SymbolType type;
    void *sub_symbol;
}Symbol;

typedef struct label_symbol{
    size_t location;
}LabelSymbol;

typedef struct jump{
    size_t offset;
    Token *label_token;
}Jmp;

typedef struct myass{
    jmp_buf          err_buf;
    LZOHTable        *registers_keywords;
    LZOHTable        *instructions_keywords;
    LZOHTable        *symbols;
    LZStack          *jumps_to_resolve;
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

static void error(MyAss *myass, Token *token, char *msg, ...);
static byte rex(
    byte w, // 64bit mode
    byte r, // extend reg field (ModRM)
    byte x, // extend index field (SIB)
    byte b  // extend r/m base field
);
static byte mod_rm(Mod mod, X64Register dest, X64Register source);
static void add_keyword(LZOHTable *keywords, const char *name, TokenType type);
static LZOHTable *create_registers_keywords(const Allocator *allocator);
static LZOHTable *create_instructions_keywords(const Allocator *allocator);
static void assemble_add_instruction(MyAss *myass, BinaryInstruction *instruction);
static void assemble_call_instruction(MyAss *myass, UnaryInstruction *instruction);
static void assemble_cmp_instruction(MyAss *myass, BinaryInstruction *instruction);
static void assemble_idiv_instruction(MyAss *myass, UnaryInstruction *instruction);
static void assemble_jcc_instructions(
	MyAss *myass,
	InstructionType type,
	UnaryInstruction *instruction
);
static void assemble_jmp_instruction(MyAss *myass, UnaryInstruction *instruction);
static void assemble_mov_instruction(MyAss *myass, BinaryInstruction *instruction);
static void assemble_imul_instruction(MyAss *myass, BinaryInstruction *instruction);
static void assemble_sub_instruction(MyAss *myass, BinaryInstruction *instruction);
static void assemble_ret_instruction(MyAss *myass);
static void assemble_xor_instruction(MyAss *myass, BinaryInstruction *instruction);
static void assemble_instruction(MyAss *myass, Instruction *instruction);
static void assemble_instructions(MyAss *myass, DynArr *instructions);
static void resolve_jumps(MyAss *myass);

void error(MyAss *myass, Token *token, char *msg, ...){
	va_list args;
	va_start(args, msg);

	fprintf(
		stderr,
		"MYASS ERROR - from line(col: %"PRId32"): %"PRId32", to line (col: %"PRId32"): %"PRId32":\n\t",
		token->start_col,
		token->start_line,
		token->end_col,
		token->end_line
	);
	vfprintf(stderr, msg, args);
	fprintf(stderr, "\n");

	va_end(args);

	longjmp(myass->err_buf, 1);
}

inline byte rex(
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

inline byte mod_rm(Mod mod, X64Register dest, X64Register source){
    return (((byte)(mod & 0x3)) << 6) | (((byte)(dest & 0x7)) << 3) | (source & 0x7);
}

void add_keyword(LZOHTable *keywords, const char *name, TokenType type){
    lzohtable_put_ckv(
        strlen(name),
        name,
        sizeof(TokenType),
        &type,
        keywords,
        NULL
    );
}

LZOHTable *create_registers_keywords(const Allocator *allocator){
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

LZOHTable *create_instructions_keywords(const Allocator *allocator){
    LZOHTable *instructions = MEMORY_LZOHTABLE(allocator);

    add_keyword(instructions, "add", ADD_TOKEN_TYPE);
    add_keyword(instructions, "call", CALL_TOKEN_TYPE);
    add_keyword(instructions, "cmp", CMP_TOKEN_TYPE);
    add_keyword(instructions, "idiv", IDIV_TOKEN_TYPE);
    add_keyword(instructions, "imul", IMUL_TOKEN_TYPE);
    add_keyword(instructions, "je", JE_TOKEN_TYPE);
    add_keyword(instructions, "jg", JG_TOKEN_TYPE);
    add_keyword(instructions, "jl", JL_TOKEN_TYPE);
    add_keyword(instructions, "jge", JGE_TOKEN_TYPE);
    add_keyword(instructions, "jle", JLE_TOKEN_TYPE);
    add_keyword(instructions, "jmp", JMP_TOKEN_TYPE);
    add_keyword(instructions, "mov", MOV_TOKEN_TYPE);
    add_keyword(instructions, "sub", SUB_TOKEN_TYPE);
    add_keyword(instructions, "ret", RET_TOKEN_TYPE);
    add_keyword(instructions, "xor", XOR_TOKEN_TYPE);

    return instructions;
}

void assemble_add_instruction(MyAss *myass, BinaryInstruction *instruction){
    Location *dst_location = instruction->dst_location;
    Location *src_location = instruction->src_location;

    switch (dst_location->type){
        case REGISTER_LOCATION_TYPE:{
            RegisterLocation *dst = dst_location->sub_location;

            switch (src_location->type){
                case LITERAL_LOCATION_TYPE:{
                    LiteralLocation *src = src_location->sub_location;

                    myass_add_r64_imm32(myass, dst->reg, src->value);

                    break;
                }case REGISTER_LOCATION_TYPE:{
                    RegisterLocation *src = src_location->sub_location;

                    myass_add_r64_r64(myass, dst->reg, src->reg);

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

void assemble_call_instruction(MyAss *myass, UnaryInstruction *instruction){
    Location *location = instruction->location;

    switch (location->type){
        case LABEL_LOCATION_TYPE:{
            myass_call_imm32(myass, 0);

            LabelLocation *label_location = location->sub_location;
            size_t offset = lzbbuff_used_bytes(myass->bbuff);
            Token *label_token = label_location->label_token;
            Jmp *jmp = MEMORY_NEW(
                ALLOCATOR,
                Jmp,
                offset,
                label_token
            );

            lzstack_push(jmp, myass->jumps_to_resolve);

            break;
        }default:{
            assert(0 && "Illegal location type");
        }
    }
}

void assemble_cmp_instruction(MyAss *myass, BinaryInstruction *instruction){
    Location *dst_location = instruction->dst_location;
    Location *src_location = instruction->src_location;

    switch (dst_location->type){
        case REGISTER_LOCATION_TYPE:{
            RegisterLocation *dst = dst_location->sub_location;

            switch (src_location->type){
                case LITERAL_LOCATION_TYPE:{
                    LiteralLocation *src = src_location->sub_location;

                    myass_cmp_r64_imm32(myass, dst->reg, src->value);

                    break;
                }case REGISTER_LOCATION_TYPE:{
                    RegisterLocation *src = src_location->sub_location;

                    myass_cmp_r64_r64(myass, dst->reg, src->reg);

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

void assemble_idiv_instruction(MyAss *myass, UnaryInstruction *instruction){
    Location *src_location = instruction->location;

    switch (src_location->type){
        case REGISTER_LOCATION_TYPE:{
            switch (src_location->type){
                case REGISTER_LOCATION_TYPE:{
                    RegisterLocation *src = src_location->sub_location;

                    myass_idiv_r64(myass, src->reg);

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

void assemble_jcc_instructions(
	MyAss *myass,
	InstructionType type,
	UnaryInstruction *instruction
){
    Location *location = instruction->location;

    switch (location->type){
        case LABEL_LOCATION_TYPE:{
            switch (type){
                case JE_INSTRUCTION_TYPE:{
                    myass_je_imm32(myass, 0);
                    break;
                }case JG_INSTRUCTION_TYPE:{
                    myass_jg_imm32(myass, 0);
                    break;
                }case JL_INSTRUCTION_TYPE:{
                    myass_jl_imm32(myass, 0);
                    break;
                }case JGE_INSTRUCTION_TYPE:{
                    myass_jge_imm32(myass, 0);
                    break;
                }case JLE_INSTRUCTION_TYPE:{
                    myass_jle_imm32(myass, 0);
                    break;
                }default:{
                    assert(0 && "Illegal instruction type");
                }
            }

            LabelLocation *label_location = location->sub_location;
            size_t offset = lzbbuff_used_bytes(myass->bbuff);
            Token *label_token = label_location->label_token;
            Jmp *jmp = MEMORY_NEW(
                ALLOCATOR,
                Jmp,
                offset,
                label_token
            );

            lzstack_push(jmp, myass->jumps_to_resolve);

            break;
        }default:{
            assert(0 && "Illegal location type");
        }
    }
}

void assemble_jmp_instruction(MyAss *myass, UnaryInstruction *instruction){
    Location *location = instruction->location;

    switch (location->type){
        case LABEL_LOCATION_TYPE:{
            myass_jmp_imm32(myass, 0);

            LabelLocation *label_location = location->sub_location;
            size_t offset = lzbbuff_used_bytes(myass->bbuff);
            Token *label_token = label_location->label_token;
            Jmp *jmp = MEMORY_NEW(
                ALLOCATOR,
                Jmp,
                offset,
                label_token
            );

            lzstack_push(jmp, myass->jumps_to_resolve);

            break;
        }default:{
            assert(0 && "Illegal location type");
        }
    }
}

void assemble_mov_instruction(MyAss *myass, BinaryInstruction *instruction){
    Location *dst_location = instruction->dst_location;
    Location *src_location = instruction->src_location;

    switch (dst_location->type){
        case REGISTER_LOCATION_TYPE:{
            RegisterLocation *dst = dst_location->sub_location;

            switch (src_location->type){
                case LITERAL_LOCATION_TYPE:{
                    LiteralLocation *src = src_location->sub_location;

                    myass_mov_r64_imm32(myass, dst->reg, src->value);

                    break;
                }case REGISTER_LOCATION_TYPE:{
                    RegisterLocation *src = src_location->sub_location;

                    myass_mov_r64_r64(myass, dst->reg, src->reg);

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

void assemble_imul_instruction(MyAss *myass, BinaryInstruction *instruction){
    Location *dst_location = instruction->dst_location;
    Location *src_location = instruction->src_location;

    switch (dst_location->type){
        case REGISTER_LOCATION_TYPE:{
            RegisterLocation *dst = dst_location->sub_location;

            switch (src_location->type){
                case REGISTER_LOCATION_TYPE:{
                    RegisterLocation *src = src_location->sub_location;

                    myass_imul_r64_r64(myass, dst->reg, src->reg);

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

void assemble_sub_instruction(MyAss *myass, BinaryInstruction *instruction){
    Location *dst_location = instruction->dst_location;
    Location *src_location = instruction->src_location;

    switch (dst_location->type){
        case REGISTER_LOCATION_TYPE:{
            RegisterLocation *dst = dst_location->sub_location;

            switch (src_location->type){
                case LITERAL_LOCATION_TYPE:{
                    LiteralLocation *src = src_location->sub_location;

                    myass_sub_r64_imm32(myass, dst->reg, src->value);

                    break;
                }case REGISTER_LOCATION_TYPE:{
                    RegisterLocation *src = src_location->sub_location;

                    myass_sub_r64_r64(myass, dst->reg, src->reg);

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

inline void assemble_ret_instruction(MyAss *myass){
    myass_ret(myass);
}

void assemble_xor_instruction(MyAss *myass, BinaryInstruction *instruction){
    Location *dst_location = instruction->dst_location;
    Location *src_location = instruction->src_location;

    switch (dst_location->type){
        case REGISTER_LOCATION_TYPE:{
            RegisterLocation *dst = dst_location->sub_location;

            switch (src_location->type){
                case LITERAL_LOCATION_TYPE:{
                    LiteralLocation *src = src_location->sub_location;

                    myass_xor_r64_imm32(myass, dst->reg, src->value);

                    break;
                }case REGISTER_LOCATION_TYPE:{
                    RegisterLocation *src = src_location->sub_location;

                    myass_xor_r64_r64(myass, dst->reg, src->reg);

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

void assemble_instruction(MyAss *myass, Instruction *instruction){
    switch (instruction->type){
        case LABEL_INSTRUCTION_TYPE:{
       		LZOHTable *symbols = myass->symbols;

         	EmptyInstruction *label_instruction = instruction->sub_instruction;
            Token *label_token = label_instruction->token;

            LabelSymbol *label_symbol = MEMORY_NEW(
                ALLOCATOR,
                LabelSymbol,
                lzbbuff_used_bytes(myass->bbuff)
            );
            Symbol symbol = {
                .type = LABEL_SYMBOL_TYPE,
                .sub_symbol = label_symbol
            };

            size_t key_size = label_token->lexeme_len;
            const char *key = label_token->lexeme;

            if(lzohtable_lookup(key_size, key, symbols, NULL)){
           		error(
             		myass,
               		label_token,
               		"Already exists symbol '%s'",
                 	key
             	);
            }

            lzohtable_put_ckv(
                key_size,
                key,
                sizeof(Symbol),
                &symbol,
                myass->symbols,
                NULL
            );

            break;
        }case ADD_INSTRUCTION_TYPE:{
            assemble_add_instruction(myass, instruction->sub_instruction);
            break;
        }case CALL_INSTRUCTION_TYPE:{
            assemble_call_instruction(myass, instruction->sub_instruction);
            break;
        }case CMP_INSTRUCTION_TYPE:{
            assemble_cmp_instruction(myass, instruction->sub_instruction);
            break;
        }case IDIV_INSTRUCTION_TYPE:{
            assemble_idiv_instruction(myass, instruction->sub_instruction);
            break;
        }case JE_INSTRUCTION_TYPE:
         case JG_INSTRUCTION_TYPE:
         case JL_INSTRUCTION_TYPE:
         case JGE_INSTRUCTION_TYPE:
         case JLE_INSTRUCTION_TYPE:{
            assemble_jcc_instructions(
            	myass,
             	instruction->type,
              	instruction->sub_instruction
            );
            break;
        }case JMP_INSTRUCTION_TYPE:{
            assemble_jmp_instruction(myass, instruction->sub_instruction);
            break;
        }case MOV_INSTRUCTION_TYPE:{
            assemble_mov_instruction(myass, instruction->sub_instruction);
            break;
        }case IMUL_INSTRUCTION_TYPE:{
            assemble_imul_instruction(myass, instruction->sub_instruction);
            break;
        }case SUB_INSTRUCTION_TYPE:{
            assemble_sub_instruction(myass, instruction->sub_instruction);
            break;
        }case RET_INSTRUCTION_TYPE:{
            assemble_ret_instruction(myass);
            break;
        }case XOR_INSTRUCTION_TYPE:{
            assemble_xor_instruction(myass, instruction->sub_instruction);
            break;
        }
    }
}

void assemble_instructions(MyAss *myass, DynArr *instructions){
    size_t len = DYNARR_LEN(instructions);

    for (size_t i = 0; i < len; i++){
        Instruction *instruction = DYNARR_GET_PTR_AS(Instruction, i, instructions);
        assemble_instruction(myass, instruction);
    }
}

void resolve_jumps(MyAss *myass){
    LZOHTable *symbols = myass->symbols;
    LZStack *jumps_to_resolve = myass->jumps_to_resolve;
    LZBBuff *bbuff = BBUFF;

    while (lzstack_peek(jumps_to_resolve)){
        Jmp *jmp = lzstack_pop(jumps_to_resolve);
        size_t jmp_offset = jmp->offset;
        Token *label_token = jmp->label_token;
        Symbol *symbol = NULL;

        if(!lzohtable_lookup(
        	label_token->lexeme_len,
            label_token->lexeme,
            symbols,
            (void **)(&symbol))
        ){
	        error(
	        	myass,
				label_token,
	         	"Unknown symbol '%s'",
	            label_token->lexeme
	        );
        }

        switch (symbol->type){
            case LABEL_SYMBOL_TYPE:{
                LabelSymbol *label_symbol = symbol->sub_symbol;
                size_t label_offset = label_symbol->location;
                dword displacement = (dword)(((dword)label_offset) - ((dword)jmp_offset));

                lzbbuff_overwrite_dword(bbuff, 0, jmp_offset - 4, displacement);

                break;
            }default:{
           		assert(0 && "Illegal symbol type");
            }
        }
    }
}

MyAss *myass_create(const Allocator *allocator){
    LZOHTable *registers_keywords = create_registers_keywords(allocator);
    LZOHTable *instructions_keywords = create_instructions_keywords(allocator);
    LZBBuff *bbuff = lzbbuff_create(8192, (LZBBuffAllocator *)allocator);
    LZArena *arena = lzarena_create((LZArenaAllocator *)allocator);
    AllocatorContext *allocator_context = MEMORY_ALLOC(AllocatorContext, 1, allocator);
    MyAss *myass = MEMORY_ALLOC(MyAss, 1, allocator);

    if(!registers_keywords || !instructions_keywords || !bbuff || !arena || !allocator_context || !myass){
        LZOHTABLE_DESTROY(registers_keywords);
        LZOHTABLE_DESTROY(instructions_keywords);
        lzbbuff_destroy(bbuff);
        lzarena_destroy(arena);
        MEMORY_DEALLOC(allocator_context, AllocatorContext, 1, allocator);
        MEMORY_DEALLOC(myass, MyAss, 1, allocator);

        return NULL;
    }

    allocator_context->err_buf = &myass->err_buf;
    allocator_context->behind_allocator = arena;

    myass->registers_keywords = registers_keywords;
    myass->instructions_keywords = instructions_keywords;
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

    LZOHTABLE_DESTROY(myass->registers_keywords);
    LZOHTABLE_DESTROY(myass->instructions_keywords);
    lzbbuff_destroy(myass->bbuff);
    MEMORY_DEALLOC(myass->arena_allocator_context, AllocatorContext, 1, allocator);
    lzarena_destroy(myass->arena);
    MEMORY_DEALLOC(myass, MyAss, 1, allocator);
}

void myass_print_as_hex(const MyAss *myass, int wprefix){
    LZBBuff *bbuff = BBUFF;

    lzbbuff_print_as_hex(bbuff, wprefix);
}

void myass_mov_r64_imm32(MyAss *myass, X64Register dst, dword src){
    LZBBuff *bbuff = BBUFF;

    lzbbuff_write_byte(bbuff, 0, rex(1, 0, 0, dst > 7));
    lzbbuff_write_byte(bbuff, 0, 0xc7);
    lzbbuff_write_byte(bbuff, 0, mod_rm(REG_MODE, 0, dst));
    lzbbuff_write_dword(bbuff, 0, src);
}

void myass_mov_r64_r64(MyAss *myass, X64Register dst, X64Register src){
    LZBBuff *bbuff = BBUFF;

    lzbbuff_write_byte(bbuff, 0, rex(1, dst > 7, 0, src > 7));
    lzbbuff_write_byte(bbuff, 0, 0x8b);
    lzbbuff_write_byte(bbuff, 0, mod_rm(REG_MODE, dst, src));
}

void myass_add_r64_r64(MyAss *myass, X64Register dst, X64Register src){
    LZBBuff *bbuff = BBUFF;

    lzbbuff_write_byte(bbuff, 0, rex(1, dst > 7, 0, src > 7));
    lzbbuff_write_byte(bbuff, 0, 0x03);
    lzbbuff_write_byte(bbuff, 0, mod_rm(REG_MODE, dst, src));
}

void myass_add_r64_imm32(MyAss *myass, X64Register dst, dword src){
    LZBBuff *bbuff = BBUFF;

    lzbbuff_write_byte(bbuff, 0, rex(1, 0, 0, dst > 7));
    lzbbuff_write_byte(bbuff, 0, 0x81);
    lzbbuff_write_byte(bbuff, 0, mod_rm(REG_MODE, 0, dst));
    lzbbuff_write_dword(bbuff, 0, src);
}

void myass_call_imm32(MyAss *myass, dword offset){
	LZBBuff *bbuff = BBUFF;

    lzbbuff_write_byte(bbuff, 0, 0xe8);
    lzbbuff_write_dword(bbuff, 0, offset);
}

void myass_cmp_r64_r64(MyAss *myass, X64Register dst, X64Register src){
    LZBBuff *bbuff = BBUFF;

    lzbbuff_write_byte(bbuff, 0, rex(1, dst > 7, 0, src > 7));
    lzbbuff_write_byte(bbuff, 0, 0x3b);
    lzbbuff_write_byte(bbuff, 0, mod_rm(REG_MODE, dst, src));
}

void myass_cmp_r64_imm32(MyAss *myass, X64Register dst, dword src){
    LZBBuff *bbuff = BBUFF;

    lzbbuff_write_byte(bbuff, 0, rex(1, 0, 0, dst > 7));
    lzbbuff_write_byte(bbuff, 0, 0x81);
    lzbbuff_write_byte(bbuff, 0, mod_rm(REG_MODE, 7, dst));
    lzbbuff_write_dword(bbuff, 0, src);
}

void myass_idiv_r64(MyAss *myass, X64Register src){
    LZBBuff *bbuff = BBUFF;

    lzbbuff_write_byte(bbuff, 0, rex(1, 0, 0, src > 7));
    lzbbuff_write_byte(bbuff, 0, 0xf7);
    lzbbuff_write_byte(bbuff, 0, mod_rm(REG_MODE, 7, src));
}

void myass_imul_r64_r64(MyAss *myass, X64Register dst, X64Register src){
    LZBBuff *bbuff = BBUFF;

    lzbbuff_write_byte(bbuff, 0, rex(1, dst > 7, 0, src > 7));
    lzbbuff_write_byte(bbuff, 0, 0x0f);
    lzbbuff_write_byte(bbuff, 0, 0xaf);
    lzbbuff_write_byte(bbuff, 0, mod_rm(REG_MODE, dst, src));
}

void myass_je_imm32(MyAss *myass, dword offset){
    LZBBuff *bbuff = BBUFF;

    lzbbuff_write_byte(bbuff, 0, 0x0f);
    lzbbuff_write_byte(bbuff, 0, 0x84);
    lzbbuff_write_dword(bbuff, 0, offset);
}

void myass_jg_imm32(MyAss *myass, dword offset){
    LZBBuff *bbuff = BBUFF;

    lzbbuff_write_byte(bbuff, 0, 0x0f);
    lzbbuff_write_byte(bbuff, 0, 0x8f);
    lzbbuff_write_dword(bbuff, 0, offset);
}

void myass_jl_imm32(MyAss *myass, dword offset){
    LZBBuff *bbuff = BBUFF;

    lzbbuff_write_byte(bbuff, 0, 0x0f);
    lzbbuff_write_byte(bbuff, 0, 0x8c);
    lzbbuff_write_dword(bbuff, 0, offset);
}

void myass_jge_imm32(MyAss *myass, dword offset){
	LZBBuff *bbuff = BBUFF;

    lzbbuff_write_byte(bbuff, 0, 0x0f);
    lzbbuff_write_byte(bbuff, 0, 0x8d);
    lzbbuff_write_dword(bbuff, 0, offset);
}

void myass_jle_imm32(MyAss *myass, dword offset){
	LZBBuff *bbuff = BBUFF;

    lzbbuff_write_byte(bbuff, 0, 0x0f);
    lzbbuff_write_byte(bbuff, 0, 0x8e);
    lzbbuff_write_dword(bbuff, 0, offset);
}

void myass_jmp_imm32(MyAss *myass, dword offset){
    LZBBuff *bbuff = BBUFF;

    lzbbuff_write_byte(bbuff, 0, 0xe9);
    lzbbuff_write_dword(bbuff, 0, offset);
}

void myass_sub_r64_r64(MyAss *myass, X64Register dst, X64Register src){
    LZBBuff *bbuff = BBUFF;

    lzbbuff_write_byte(bbuff, 0, rex(1, dst > 7, 0, src > 7));
    lzbbuff_write_byte(bbuff, 0, 0x2b);
    lzbbuff_write_byte(bbuff, 0, mod_rm(REG_MODE, dst, src));
}

void myass_sub_r64_imm32(MyAss *myass, X64Register dst, dword src){
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

void myass_xor_r64_r64(MyAss *myass, X64Register dst, X64Register src){
    LZBBuff *bbuff = BBUFF;

    lzbbuff_write_byte(bbuff, 0, rex(1, dst > 7, 0, src > 7));
    lzbbuff_write_byte(bbuff, 0, 0x33);
    lzbbuff_write_byte(bbuff, 0, mod_rm(REG_MODE, dst, src));
}

void myass_xor_r64_imm32(MyAss *myass, X64Register dst, dword src){
    LZBBuff *bbuff = BBUFF;

    lzbbuff_write_byte(bbuff, 0, rex(1, 0, 0, dst > 7));
    lzbbuff_write_byte(bbuff, 0, 0x81);
    lzbbuff_write_byte(bbuff, 0, mod_rm(REG_MODE, 6, dst));
    lzbbuff_write_dword(bbuff, 0, src);
}

int myass_assemble(MyAss *myass, size_t input_len, const char *input){
    if(setjmp(myass->err_buf) == 0){
        lzbbuff_restart(BBUFF);

        LZOHTable *registers_keywords = myass->registers_keywords;
        LZOHTable *instructions_keywords = myass->instructions_keywords;
        LZOHTable *symbols = MEMORY_LZOHTABLE(ALLOCATOR);
        LZStack *jumps_to_resolve = MEMORY_LZSTACK(ALLOCATOR);
        DynArr *tokens = MEMORY_DYNARR_PTR(ALLOCATOR);
        DynArr *instructions = MEMORY_DYNARR_PTR(ALLOCATOR);
        BStr code = {.len = input_len, .buff = input};
        Lexer *lexer = lexer_create(ALLOCATOR);
        Parser *parser = parser_create(ALLOCATOR);

        myass->symbols = symbols;
        myass->jumps_to_resolve = jumps_to_resolve;

        if(lexer_lex(lexer, registers_keywords, instructions_keywords, &code, tokens)){
            return 1;
        }

        if(parser_parse(parser, tokens, instructions)){
            return 1;
        }

        assemble_instructions(myass, instructions);
        resolve_jumps(myass);
        lzarena_free_all(ARENA);

        myass->symbols = NULL;
        myass->jumps_to_resolve = NULL;

        return 0;
    }else{
        return 1;
    }
}
