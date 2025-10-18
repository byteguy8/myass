#include "myass.h"
#include "dynarr.h"
#include "essentials/lzohtable.h"
#include "essentials/lzarena.h"
#include "essentials/lzbbuff.h"
#include "essentials/memory.h"

#include "lzbstr.h"
#include "token.h"
#include "lexer.h"
#include "parser.h"

#include "location.h"
#include "instruction.h"
#include "types.h"

#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <setjmp.h>
#include <stdarg.h>
#include <inttypes.h>

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
    size_t           largest_instruction;
    DynArr           *instructions;
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

//------------------------------------------------------------------------------------//
//                                 PRIVATE INTERFACE                                  //
//------------------------------------------------------------------------------------//
#define ARENA (myass->arena)
#define ALLOCATOR (&(myass->arena_allocator))
#define BBUFF (myass->bbuff)

static void error(MyAss *myass, Token *token, char *msg, ...);

static byte rex(
    byte w, // 64 bits mode
    byte r, // extend reg field (ModRM)
    byte x, // extend index field (SIB)
    byte b  // extend r/m base field
);
static byte mod_rm(Mod mod, X64Register dest, X64Register source);
static void add_keyword(LZOHTable *keywords, const char *name, TokenType type);
static LZOHTable *create_registers_keywords(const Allocator *allocator);
static LZOHTable *create_instructions_keywords(const Allocator *allocator);

static void reg_to_str(LZBStr *lzbstr, X64Register reg);
static void location_to_str(LZBStr *lzbstr, Location *location);
static void instruction_to_str(LZBStr *lzbstr, Instruction *instruction);

static void assemble_add_instruction(MyAss *myass, BinaryInstruction *instruction);
static void assemble_call_instruction(MyAss *myass, UnaryInstruction *instruction);
static void assemble_cmp_instruction(MyAss *myass, BinaryInstruction *instruction);
static void assemble_idiv_instruction(MyAss *myass, UnaryInstruction *instruction);
static void assemble_imul_instruction(MyAss *myass, BinaryInstruction *instruction);
static void assemble_jcc_instructions(
	MyAss *myass,
	InstructionType type,
	UnaryInstruction *instruction
);
static void assemble_jmp_instruction(MyAss *myass, UnaryInstruction *instruction);
static void assemble_mov_instruction(MyAss *myass, BinaryInstruction *instruction);
static void assemble_pop_instruction(MyAss *myass, UnaryInstruction *instruction);
static void assemble_push_instruction(MyAss *myass, UnaryInstruction *instruction);
static void assemble_sub_instruction(MyAss *myass, BinaryInstruction *instruction);
static void assemble_ret_instruction(MyAss *myass);
static void assemble_xor_instruction(MyAss *myass, BinaryInstruction *instruction);

static void assemble_instruction(MyAss *myass, Instruction *instruction);
static void assemble_instructions(MyAss *myass, DynArr *instructions);
static void resolve_jumps(MyAss *myass);

//------------------------------------------------------------------------------------//
//                               PRIVATE IMPLEMENTATION                               //
//------------------------------------------------------------------------------------//
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
    add_keyword(instructions, "push", PUSH_TOKEN_TYPE);
    add_keyword(instructions, "pop", POP_TOKEN_TYPE);
    add_keyword(instructions, "sub", SUB_TOKEN_TYPE);
    add_keyword(instructions, "ret", RET_TOKEN_TYPE);
    add_keyword(instructions, "xor", XOR_TOKEN_TYPE);

    return instructions;
}

void reg_to_str(LZBStr *lzbstr, X64Register reg){
	switch (reg) {
		case RAX:{
			lzbstr_append("rax", lzbstr);
			break;
		}case RCX:{
			lzbstr_append("rcx", lzbstr);
			break;
		}case RDX:{
			lzbstr_append("rdx", lzbstr);
			break;
		}case RBX:{
			lzbstr_append("rbx", lzbstr);
			break;
		}case RSP:{
			lzbstr_append("rsp", lzbstr);
			break;
		}case RBP:{
			lzbstr_append("rbp", lzbstr);
			break;
		}case RSI:{
			lzbstr_append("rsi", lzbstr);
			break;
		}case RDI:{
			lzbstr_append("rdi", lzbstr);
			break;
		}case R8:{
			lzbstr_append("r8", lzbstr);
			break;
		}case R9:{
			lzbstr_append("r9", lzbstr);
			break;
		}case R10:{
			lzbstr_append("r10", lzbstr);
			break;
		}case R11:{
			lzbstr_append("r11", lzbstr);
			break;
		}case R12:{
			lzbstr_append("r12", lzbstr);
			break;
		}case R13:{
			lzbstr_append("r13", lzbstr);
			break;
		}case R14:{
			lzbstr_append("r14", lzbstr);
			break;
		}case R15:{
			lzbstr_append("r15", lzbstr);
			break;
		}
	}
}

void location_to_str(LZBStr *lzbstr, Location *location){
	switch (location->type) {
		case LITERAL_LOCATION_TYPE:{
			LiteralLocation *literal_location = location->sub_location;

			lzbstr_append_args(lzbstr, "%"PRId32, literal_location->value);

			break;
		}case REGISTER_LOCATION_TYPE:{
			RegisterLocation *reg_location = location->sub_location;

			reg_to_str(lzbstr, reg_location->reg);

			break;
		}case LABEL_LOCATION_TYPE:{
			LabelLocation *label_location = location->sub_location;

			lzbstr_append_args(lzbstr, "%s", label_location->label_token->lexeme);

			break;
		}
    }
}

void instruction_to_str(LZBStr *lzbstr, Instruction *instruction){
	switch (instruction->type) {
		case LABEL_INSTRUCTION_TYPE:{
			break;
		}case ADD_INSTRUCTION_TYPE:{
			BinaryInstruction *add_instruction = instruction->sub_instruction;

			lzbstr_append("add ", lzbstr);
			location_to_str(lzbstr, add_instruction->dst_location);
			lzbstr_append(", ", lzbstr);
			location_to_str(lzbstr, add_instruction->src_location);

			break;
		}case CALL_INSTRUCTION_TYPE:{
			UnaryInstruction *call_instruction = instruction->sub_instruction;

			lzbstr_append("call ", lzbstr);
			location_to_str(lzbstr, call_instruction->location);

		    break;
	    }case CMP_INSTRUCTION_TYPE:{
			BinaryInstruction *cmp_instruction = instruction->sub_instruction;

			lzbstr_append("cmp ", lzbstr);
			location_to_str(lzbstr, cmp_instruction->dst_location);
			lzbstr_append(", ", lzbstr);
			location_to_str(lzbstr, cmp_instruction->src_location);

   			break;
	    }case IDIV_INSTRUCTION_TYPE:{
			BinaryInstruction *div_instruction = instruction->sub_instruction;

			lzbstr_append("div ", lzbstr);
			location_to_str(lzbstr, div_instruction->dst_location);
			lzbstr_append(", ", lzbstr);
			location_to_str(lzbstr, div_instruction->src_location);

		    break;
	    }case IMUL_INSTRUCTION_TYPE:{
			BinaryInstruction *imul_instruction = instruction->sub_instruction;

			lzbstr_append("imul ", lzbstr);
			location_to_str(lzbstr, imul_instruction->dst_location);
			lzbstr_append(", ", lzbstr);
			location_to_str(lzbstr, imul_instruction->src_location);

		    break;
	    }case JE_INSTRUCTION_TYPE:{
			UnaryInstruction *je_instruction = instruction->sub_instruction;

			lzbstr_append("je ", lzbstr);
			location_to_str(lzbstr, je_instruction->location);

			break;
	    }case JG_INSTRUCTION_TYPE:{
			UnaryInstruction *jg_instruction = instruction->sub_instruction;

			lzbstr_append("jg ", lzbstr);
			location_to_str(lzbstr, jg_instruction->location);

			break;
	    }case JL_INSTRUCTION_TYPE:{
			UnaryInstruction *jl_instruction = instruction->sub_instruction;

			lzbstr_append("jl ", lzbstr);
			location_to_str(lzbstr, jl_instruction->location);

		    break;
	    }case JGE_INSTRUCTION_TYPE:{
			UnaryInstruction *jge_instruction = instruction->sub_instruction;

			lzbstr_append("jge ", lzbstr);
			location_to_str(lzbstr, jge_instruction->location);

		    break;
	    }case JLE_INSTRUCTION_TYPE:{
			UnaryInstruction *jle_instruction = instruction->sub_instruction;

			lzbstr_append("jle ", lzbstr);
			location_to_str(lzbstr, jle_instruction->location);

		    break;
	    }case JMP_INSTRUCTION_TYPE:{
			UnaryInstruction *jmp_instruction = instruction->sub_instruction;

			lzbstr_append("jmp ", lzbstr);
			location_to_str(lzbstr, jmp_instruction->location);

		    break;
	    }case MOV_INSTRUCTION_TYPE:{
			BinaryInstruction *mov_instruction = instruction->sub_instruction;

			lzbstr_append("mov ", lzbstr);
			location_to_str(lzbstr, mov_instruction->dst_location);
			lzbstr_append(", ", lzbstr);
			location_to_str(lzbstr, mov_instruction->src_location);

		    break;
	    }case POP_INSTRUCTION_TYPE:{
			UnaryInstruction *pop_instruction = instruction->sub_instruction;

			lzbstr_append("pop ", lzbstr);
			location_to_str(lzbstr, pop_instruction->location);

		    break;
	    }case PUSH_INSTRUCTION_TYPE:{
			UnaryInstruction *push_instruction = instruction->sub_instruction;

			lzbstr_append("push ", lzbstr);
			location_to_str(lzbstr, push_instruction->location);

		    break;
	    }case SUB_INSTRUCTION_TYPE:{
			BinaryInstruction *sub_instruction = instruction->sub_instruction;

			lzbstr_append("sub ", lzbstr);
			location_to_str(lzbstr, sub_instruction->dst_location);
			lzbstr_append(", ", lzbstr);
			location_to_str(lzbstr, sub_instruction->src_location);

			break;
	    }case RET_INSTRUCTION_TYPE:{
			lzbstr_append("ret", lzbstr);
		    break;
	    }case XOR_INSTRUCTION_TYPE:{
			BinaryInstruction *xor_instruction = instruction->sub_instruction;

			lzbstr_append("xor ", lzbstr);
			location_to_str(lzbstr, xor_instruction->dst_location);
			lzbstr_append(", ", lzbstr);
			location_to_str(lzbstr, xor_instruction->src_location);

		    break;
	    }
	}
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

void assemble_pop_instruction(MyAss *myass, UnaryInstruction *instruction){
	Location *dst_location = instruction->location;

	switch (dst_location->type) {
		case REGISTER_LOCATION_TYPE:{
			RegisterLocation *reg_location = dst_location->sub_location;

			myass_pop_r64(myass, reg_location->reg);

			break;
		}default:{
			assert(0 && "Illegal location type");
		}
	}
}

void assemble_push_instruction(MyAss *myass, UnaryInstruction *instruction){
	Location *src_location = instruction->location;

	switch (src_location->type) {
		case REGISTER_LOCATION_TYPE:{
			RegisterLocation *reg_location = src_location->sub_location;

			myass_push_r64(myass, reg_location->reg);

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
        }case POP_INSTRUCTION_TYPE:{
            assemble_pop_instruction(myass, instruction->sub_instruction);
            break;
        }case PUSH_INSTRUCTION_TYPE:{
            assemble_push_instruction(myass, instruction->sub_instruction);
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
	LZBBuff *bbuff = BBUFF;
	size_t len = DYNARR_LEN(instructions);

    for (size_t i = 0; i < len; i++){
        Instruction *instruction = DYNARR_GET_PTR_AS(Instruction, i, instructions);
        size_t used_before = lzbbuff_used_bytes(bbuff);

        assemble_instruction(myass, instruction);

        size_t used_after = lzbbuff_used_bytes(bbuff);
        size_t instruction_len = used_after - used_before;

        instruction->offset = used_before;
        instruction->len = instruction_len;

        if(instruction_len > myass->largest_instruction){
            myass->largest_instruction = instruction_len;
        }
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

//------------------------------------------------------------------------------------//
//                               PUBLIC IMPLEMENTATION                                //
//------------------------------------------------------------------------------------//
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
    myass->largest_instruction = 0;
    myass->instructions = NULL;
    myass->symbols = NULL;
    myass->jumps_to_resolve = NULL;
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

void myass_formatted_print_hex(const MyAss *myass){
	size_t offset_len = 6;
	size_t size_len = 6;
	size_t others_len = 5;
	size_t largest_bytes_len = myass->largest_instruction * 2;
	size_t spacing_len = (myass->largest_instruction - 1) * 2;
	size_t largest_line_len = offset_len + size_len + others_len + largest_bytes_len + spacing_len;

	LZBStr *lzbstr = MEMORY_LZBSTR(ALLOCATOR);
	DynArr *instructions = myass->instructions;
	LZBBuff *bbuff = BBUFF;
	size_t len = DYNARR_LEN(instructions);

	for (size_t i = 0; i < len; i++) {
		Instruction *instruction = dynarr_get_ptr(i, instructions);
		size_t instruction_offset = instruction->offset;
		size_t instruction_len = instruction->len;

		if(instruction->type == LABEL_INSTRUCTION_TYPE){
			EmptyInstruction *label_instruction = instruction->sub_instruction;

			printf("%s:", label_instruction->token->lexeme);

			if(i + 1 < len){
				printf("\n");
			}
		}

		if(instruction_len == 0){
			continue;
		}

		printf("%06x", (unsigned int)instruction_offset);
		printf(" - %06zu", instruction_len);
		printf(": ");

		for (size_t o = 0; o < instruction_len; o++) {
			byte b = bbuff->raw_buff[instruction_offset + o];

			printf("%02x", b);

			if(o + 1 < instruction_len){
				printf(", ");
			}
		}

		size_t line_len = offset_len + size_len + others_len + (instruction_len * 2) + ((instruction_len - 1) * 2);

		instruction_to_str(lzbstr, instruction);
		printf(
			"%*s%s",
			(int)(largest_line_len - line_len + 8),
			"",
			lzbstr->buff
		);
		lzbstr_reset(lzbstr);

		if(i + 1 < len){
			printf("\n");
		}
	}

	printf("\n");
}

void myass_add_r64_imm32(MyAss *myass, X64Register dst, dword src){
    LZBBuff *bbuff = BBUFF;

    lzbbuff_write_byte(bbuff, 0, rex(1, 0, 0, dst > 7));
    lzbbuff_write_byte(bbuff, 0, 0x81);
    lzbbuff_write_byte(bbuff, 0, mod_rm(REG_MODE, 0, dst));
    lzbbuff_write_dword(bbuff, 0, src);
}

void myass_add_r64_r64(MyAss *myass, X64Register dst, X64Register src){
    LZBBuff *bbuff = BBUFF;

    lzbbuff_write_byte(bbuff, 0, rex(1, dst > 7, 0, src > 7));
    lzbbuff_write_byte(bbuff, 0, 0x03);
    lzbbuff_write_byte(bbuff, 0, mod_rm(REG_MODE, dst, src));
}

void myass_call_imm32(MyAss *myass, dword offset){
	LZBBuff *bbuff = BBUFF;

    lzbbuff_write_byte(bbuff, 0, 0xe8);
    lzbbuff_write_dword(bbuff, 0, offset);
}

void myass_cmp_r64_imm32(MyAss *myass, X64Register dst, dword src){
    LZBBuff *bbuff = BBUFF;

    lzbbuff_write_byte(bbuff, 0, rex(1, 0, 0, dst > 7));
    lzbbuff_write_byte(bbuff, 0, 0x81);
    lzbbuff_write_byte(bbuff, 0, mod_rm(REG_MODE, 7, dst));
    lzbbuff_write_dword(bbuff, 0, src);
}

void myass_cmp_r64_r64(MyAss *myass, X64Register dst, X64Register src){
    LZBBuff *bbuff = BBUFF;

    lzbbuff_write_byte(bbuff, 0, rex(1, dst > 7, 0, src > 7));
    lzbbuff_write_byte(bbuff, 0, 0x3b);
    lzbbuff_write_byte(bbuff, 0, mod_rm(REG_MODE, dst, src));
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

void myass_pop_r64(MyAss *myass, X64Register dst){
	LZBBuff *bbuff = BBUFF;

	if(dst > 7) lzbbuff_write_byte(bbuff, 0, rex(0, 0, 0, 1));
    lzbbuff_write_byte(bbuff, 0, 0x58 | (((byte)dst) & 0b00000111));
}

void myass_push_r64(MyAss *myass, X64Register src){
	LZBBuff *bbuff = BBUFF;

	if(src > 7) lzbbuff_write_byte(bbuff, 0, rex(0, 0, 0, 1));
    lzbbuff_write_byte(bbuff, 0, 0x50 | (((byte)src) & 0b00000111));
}

void myass_sub_r64_imm32(MyAss *myass, X64Register dst, dword src){
    LZBBuff *bbuff = BBUFF;

    lzbbuff_write_byte(bbuff, 0, rex(1, 0, 0, dst > 7));
    lzbbuff_write_byte(bbuff, 0, 0x81);
    lzbbuff_write_byte(bbuff, 0, mod_rm(REG_MODE, 5, dst));
    lzbbuff_write_dword(bbuff, 0, src);
}

void myass_sub_r64_r64(MyAss *myass, X64Register dst, X64Register src){
    LZBBuff *bbuff = BBUFF;

    lzbbuff_write_byte(bbuff, 0, rex(1, dst > 7, 0, src > 7));
    lzbbuff_write_byte(bbuff, 0, 0x2b);
    lzbbuff_write_byte(bbuff, 0, mod_rm(REG_MODE, dst, src));
}

void myass_ret(MyAss *myass){
    LZBBuff *bbuff = BBUFF;

    lzbbuff_write_byte(bbuff, 0, 0xc3);
}

void myass_xor_r64_imm32(MyAss *myass, X64Register dst, dword src){
    LZBBuff *bbuff = BBUFF;

    lzbbuff_write_byte(bbuff, 0, rex(1, 0, 0, dst > 7));
    lzbbuff_write_byte(bbuff, 0, 0x81);
    lzbbuff_write_byte(bbuff, 0, mod_rm(REG_MODE, 6, dst));
    lzbbuff_write_dword(bbuff, 0, src);
}

void myass_xor_r64_r64(MyAss *myass, X64Register dst, X64Register src){
    LZBBuff *bbuff = BBUFF;

    lzbbuff_write_byte(bbuff, 0, rex(1, dst > 7, 0, src > 7));
    lzbbuff_write_byte(bbuff, 0, 0x33);
    lzbbuff_write_byte(bbuff, 0, mod_rm(REG_MODE, dst, src));
}

int myass_assemble(MyAss *myass, size_t input_len, const char *input){
    if(setjmp(myass->err_buf) == 0){
        lzbbuff_restart(BBUFF);
        lzarena_free_all(ARENA);

        LZOHTable *registers_keywords = myass->registers_keywords;
        LZOHTable *instructions_keywords = myass->instructions_keywords;
        LZOHTable *symbols = MEMORY_LZOHTABLE(ALLOCATOR);
        LZStack *jumps_to_resolve = MEMORY_LZSTACK(ALLOCATOR);
        DynArr *tokens = MEMORY_DYNARR_PTR(ALLOCATOR);
        DynArr *instructions = MEMORY_DYNARR_PTR(ALLOCATOR);
        BStr code = {.len = input_len, .buff = input};
        Lexer *lexer = lexer_create(ALLOCATOR);
        Parser *parser = parser_create(ALLOCATOR);

        myass->largest_instruction = 0;
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

        myass->instructions = instructions;
        myass->symbols = NULL;
        myass->jumps_to_resolve = NULL;

        return 0;
    }else{
        return 1;
    }
}
