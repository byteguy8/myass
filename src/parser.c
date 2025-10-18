#include "parser.h"
#include "token.h"
#include "instruction.h"
#include "lzbbuff.h"

#include <setjmp.h>
#include <stdarg.h>
#include <stdio.h>
#include <assert.h>
#include <inttypes.h>

#define ALLOCATOR (parser->allocator)
#define CURRENT_LEXEME (peek(parser)->lexeme)

//------------------------------------------------------------
//                      PRIVATE INTERFACE                   //
//------------------------------------------------------------
static void error(Parser *parser, Token *token, char *fmt, ...);

static inline Token *peek(const Parser *parser);
static inline Token *previous(const Parser *parser);
static inline Token *advance(Parser *parser);
static inline int is_at_end(const Parser *parser);
static int match(Parser *parser, size_t types_count, ...);
static inline int check(Parser *parser, TokenType type);
static Token *consume(Parser *parser, TokenType type, char *fmt, ...);

static X64Register token_to_register(Token *token);
static Location *create_register_location(Parser *parser, X64Register reg);
static Location *create_literal_location(Parser *parser, dword value);
static Location *create_label_location(Parser *parser, Token *label_token);
static Location *token_to_location(Parser *parser, Token *location_token);

static Instruction *parse_label_instruction(Parser *parser);
static Instruction *parse_add_instruction(Parser *parser);
static Instruction *parse_call_instruction(Parser *parser);
static Instruction *parse_cmp_instruction(Parser *parser);
static Instruction *parse_idiv_instruction(Parser *parser);
static Instruction *parse_imul_instruction(Parser *parser);
static Instruction *parse_jcc_instruction(Parser *parser);
static Instruction *parse_jmp_instruction(Parser *parser);
static Instruction *parse_mov_instruction(Parser *parser);
static Instruction *parse_pop_instruction(Parser *parser);
static Instruction *parse_push_instruction(Parser *parser);
static Instruction *parse_sub_instruction(Parser *parser);
static Instruction *parse_ret_instruction(Parser *parser);
static Instruction *parse_xor_instruction(Parser *parser);
static Instruction *parse_instruction(Parser *parser);
//------------------------------------------------------------
//                 PRIVATE IMPLEMENTATOIN                   //
//------------------------------------------------------------
static void error(Parser *parser, Token *token, char *fmt, ...){
    va_list args;
    va_start(args, fmt);

    fprintf(
		stderr,
		"PARSER ERROR - from line(col: %"PRId32"): %"PRId32", to line(col: %"PRId32"): %"PRId32":\n\t",
		token->start_col,
		token->start_line,
		token->end_col,
		token->end_line
	);
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n");

    va_end(args);

    longjmp(parser->err_buf, 1);
}

static inline Token *peek(const Parser *parser){
    return (Token *)dynarr_get_ptr(parser->current, parser->tokens);
}

static inline Token *previous(const Parser *parser){
    return (Token *)dynarr_get_ptr(parser->current - 1, parser->tokens);
}

static inline Token *advance(Parser *parser){
    return (Token *)dynarr_get_ptr(parser->current++, parser->tokens);
}

static inline int is_at_end(const Parser *parser){
    const Token *token = peek(parser);
    return token->type == EOF_TOKEN_TYPE;
}

static int match(Parser *parser, size_t types_count, ...){
    va_list args;
    va_start(args, types_count);

    const Token *token = peek(parser);

    for (size_t i = 0; i < types_count; i++){
        TokenType type = va_arg(args, TokenType);

        if(type == token->type){
            advance(parser);
            return 1;
        }
    }

    va_end(args);

    return 0;
}

static inline int check(Parser *parser, TokenType type){
    return peek(parser)->type == type;
}

static Token *consume(Parser *parser, TokenType type, char *fmt, ...){
    Token *token = peek(parser);

    if(type == token->type){
        advance(parser);
        return token;
    }

    va_list args;
    va_start(args, fmt);

    fprintf(
		stderr,
		"PARSER ERROR - from line(col: %"PRId32"): %"PRId32", to line(col: %"PRId32"): %"PRId32":\n\t",
		token->start_col,
		token->start_line,
		token->end_col,
		token->end_line
	);
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n");

    va_end(args);

    longjmp(parser->err_buf, 1);

    return NULL;
}

X64Register token_to_register(Token *token){
    if(strncmp("rax", token->lexeme, 3) == 0)      return RAX;
    else if(strncmp("rcx", token->lexeme, 3) == 0) return RCX;
    else if(strncmp("rdx", token->lexeme, 3) == 0) return RDX;
    else if(strncmp("rbx", token->lexeme, 3) == 0) return RBX;
    else if(strncmp("rsp", token->lexeme, 3) == 0) return RSP;
    else if(strncmp("rbp", token->lexeme, 3) == 0) return RBP;
    else if(strncmp("rsi", token->lexeme, 3) == 0) return RSI;
    else if(strncmp("rdi", token->lexeme, 3) == 0) return RDI;
    else if(strncmp("r8", token->lexeme, 3) == 0)  return R8;
    else if(strncmp("r9", token->lexeme, 3) == 0)  return R9;
    else if(strncmp("r10", token->lexeme, 3) == 0) return R10;
    else if(strncmp("r11", token->lexeme, 3) == 0) return R11;
    else if(strncmp("r12", token->lexeme, 3) == 0) return R12;
    else if(strncmp("r13", token->lexeme, 3) == 0) return R13;
    else if(strncmp("r14", token->lexeme, 3) == 0) return R14;
    else if(strncmp("r15", token->lexeme, 3) == 0) return R15;

    assert(0 && "Illegal token type");

    return -1;
}

Location *create_register_location(Parser *parser, X64Register reg){
    RegisterLocation *register_location = MEMORY_NEW(
        ALLOCATOR,
        RegisterLocation,
        reg
    );

    return MEMORY_NEW(
        ALLOCATOR,
        Location,
        REGISTER_LOCATION_TYPE,
        register_location
    );
}

Location *create_label_location(Parser *parser, Token *label_token){
    LabelLocation *label_location = MEMORY_NEW(
        ALLOCATOR,
        LabelLocation,
        label_token
    );

    return MEMORY_NEW(
        ALLOCATOR,
        Location,
        LABEL_LOCATION_TYPE,
        label_location
    );
}

Location *create_literal_location(Parser *parser, dword value){
    LiteralLocation *literal_location = MEMORY_NEW(
        ALLOCATOR,
        LiteralLocation,
        value
    );

    return MEMORY_NEW(
        ALLOCATOR,
        Location,
        LITERAL_LOCATION_TYPE,
        literal_location
    );
}

Location *token_to_location(Parser *parser, Token *location_token){
    switch (location_token->type){
        case DWORD_TYPE_TOKEN_TYPE:{
            dword value = *(dword *)location_token->literal;

            return create_literal_location(parser, value);
        }case REGISTER_TOKEN_TYPE:{
            X64Register reg = token_to_register(location_token);

            return create_register_location(parser, reg);
        }case IDENTIFIER_TOKEN_TYPE:{
            return create_label_location(parser, location_token);
        }default:{
            assert("Illegal token type");
        }
    }

    return NULL;
}

Instruction *parse_label_instruction(Parser *parser){
	Token *label_token = previous(parser);

    consume(
        parser,
        COLON_TOKEN_TYPE,
        "Expect ':' token after label, but got: '%s'",
        CURRENT_LEXEME
    );

    EmptyInstruction *instruction = MEMORY_NEW(
        ALLOCATOR,
        EmptyInstruction,
        label_token
    );

    return MEMORY_NEW(
        ALLOCATOR,
        Instruction,
        0,
        0,
        LABEL_INSTRUCTION_TYPE,
        instruction
    );
}

Instruction *parse_add_instruction(Parser *parser){
	Token *instruction_token = previous(parser);
    Token *dst_token = consume(
        parser,
        REGISTER_TOKEN_TYPE,
        "Expect register, but got: '%s'",
        CURRENT_LEXEME
    );

    consume(
        parser,
        COMMA_TOKEN_TYPE,
        "Expect ',', but got: '%s'",
        CURRENT_LEXEME
    );

    Token *src_token = NULL;

    if(match(parser, 2, DWORD_TYPE_TOKEN_TYPE, REGISTER_TOKEN_TYPE)){
        src_token = previous(parser);
    }

    if(!src_token){
        error(
            parser,
            peek(parser),
            "Expect literal or register, but got: '%s'",
            CURRENT_LEXEME
        );
    }

    BinaryInstruction *instruction = MEMORY_NEW(
        ALLOCATOR,
        BinaryInstruction,
        token_to_location(parser, dst_token),
        token_to_location(parser, src_token),
        instruction_token,
        dst_token,
        src_token
    );

    return MEMORY_NEW(
        ALLOCATOR,
        Instruction,
        0,
        0,
        ADD_INSTRUCTION_TYPE,
        instruction
    );
}

Instruction *parse_call_instruction(Parser *parser){
	Token *instruction_token = previous(parser);
    Token *label_token = consume(
        parser,
        IDENTIFIER_TOKEN_TYPE,
        "Expect label after instruction, but got: '%s'",
        CURRENT_LEXEME
    );

    UnaryInstruction *instruction = MEMORY_NEW(
        ALLOCATOR,
        UnaryInstruction,
        token_to_location(parser, label_token),
        instruction_token,
        label_token,
    );

    return MEMORY_NEW(
        ALLOCATOR,
        Instruction,
        0,
        0,
        CALL_INSTRUCTION_TYPE,
        instruction
    );
}

Instruction *parse_cmp_instruction(Parser *parser){
	Token *instruction_token = previous(parser);
    Token *dst_token = consume(
        parser,
        REGISTER_TOKEN_TYPE,
        "Expect register, but got: '%s'",
        CURRENT_LEXEME
    );

    consume(
        parser,
        COMMA_TOKEN_TYPE,
        "Expect ',' token after dest operand, but got: '%s'",
        CURRENT_LEXEME
    );

    Token *src_token = NULL;

    if(match(parser, 2, DWORD_TYPE_TOKEN_TYPE, REGISTER_TOKEN_TYPE)){
        src_token = previous(parser);
    }

    if(!src_token){
        error(
            parser,
            instruction_token,
            "Expect immediate value or register after ',' token as source operand, but got: '%s'",
            CURRENT_LEXEME
        );
    }

    BinaryInstruction *instruction = MEMORY_NEW(
        ALLOCATOR,
        BinaryInstruction,
        token_to_location(parser, dst_token),
        token_to_location(parser, src_token),
        instruction_token,
        dst_token,
        src_token
    );

    return MEMORY_NEW(
        ALLOCATOR,
        Instruction,
        0,
        0,
        CMP_INSTRUCTION_TYPE,
        instruction
    );
}

Instruction *parse_idiv_instruction(Parser *parser){
	Token *instruction_token = previous(parser);
    Token *src_token = consume(
        parser,
        REGISTER_TOKEN_TYPE,
        "Expect register, but got: '%s'",
        CURRENT_LEXEME
    );

    UnaryInstruction *instruction = MEMORY_NEW(
        ALLOCATOR,
        UnaryInstruction,
        token_to_location(parser, src_token),
        instruction_token,
        src_token,
    );

    return MEMORY_NEW(
        ALLOCATOR,
        Instruction,
        0,
        0,
        IDIV_INSTRUCTION_TYPE,
        instruction
    );
}

Instruction *parse_imul_instruction(Parser *parser){
	Token *instruction_token = previous(parser);
    Token *dst_token = consume(
        parser,
        REGISTER_TOKEN_TYPE,
        "Expect register, but got: '%s'",
        CURRENT_LEXEME
    );

    consume(
        parser,
        COMMA_TOKEN_TYPE,
        "Expect ',', but got: '%s'",
        CURRENT_LEXEME
    );

    Token *src_token = consume(
        parser,
        REGISTER_TOKEN_TYPE,
        "Expect register, but got: '%s'",
        CURRENT_LEXEME
    );

    if(!src_token){
        error(
            parser,
            peek(parser),
            "Expect literal or register, but got: '%s'",
            CURRENT_LEXEME
        );
    }

    BinaryInstruction *instruction = MEMORY_NEW(
        ALLOCATOR,
        BinaryInstruction,
        token_to_location(parser, dst_token),
        token_to_location(parser, src_token),
        instruction_token,
        dst_token,
        src_token
    );

    return MEMORY_NEW(
        ALLOCATOR,
        Instruction,
        0,
        0,
        IMUL_INSTRUCTION_TYPE,
        instruction
    );
}

Instruction *parse_jcc_instruction(Parser *parser){
	Token *instruction_token = previous(parser);
    Token *label_token = consume(
        parser,
        IDENTIFIER_TOKEN_TYPE,
        "Expect label name after '%s' instruction, but got: '%s'",
        instruction_token->lexeme,
        CURRENT_LEXEME
    );

    UnaryInstruction *instruction = MEMORY_NEW(
        ALLOCATOR,
        UnaryInstruction,
        token_to_location(parser, label_token),
        instruction_token,
        label_token,
    );

    InstructionType type;

    switch (instruction_token->type){
        case JE_TOKEN_TYPE:{
            type = JE_INSTRUCTION_TYPE;
            break;
        }case JG_TOKEN_TYPE:{
            type = JG_INSTRUCTION_TYPE;
            break;
        }case JL_TOKEN_TYPE:{
            type = JL_INSTRUCTION_TYPE;
            break;
        }case JGE_TOKEN_TYPE:{
            type = JGE_INSTRUCTION_TYPE;
            break;
        }case JLE_TOKEN_TYPE:{
            type = JLE_INSTRUCTION_TYPE;
            break;
        }default:{
            assert(0 && "Illegal token type");
        }
    }

    return MEMORY_NEW(
        ALLOCATOR,
        Instruction,
        0,
        0,
        type,
        instruction
    );
}

Instruction *parse_jmp_instruction(Parser *parser){
	Token *instruction_token = previous(parser);
    Token *label_token = consume(
        parser,
        IDENTIFIER_TOKEN_TYPE,
        "Expect label name after jmp instruction, but got: '%s'",
        CURRENT_LEXEME
    );

    UnaryInstruction *instruction = MEMORY_NEW(
        ALLOCATOR,
        UnaryInstruction,
        token_to_location(parser, label_token),
        instruction_token,
        label_token,
    );

    return MEMORY_NEW(
        ALLOCATOR,
        Instruction,
        0,
        0,
        JMP_INSTRUCTION_TYPE,
        instruction
    );
}

Instruction *parse_mov_instruction(Parser *parser){
	Token *instruction_token = previous(parser);
    Token *dst_token = consume(
        parser,
        REGISTER_TOKEN_TYPE,
        "Expect register as destination operand, but got: '%s'",
        CURRENT_LEXEME
    );

    consume(
        parser,
        COMMA_TOKEN_TYPE,
        "Expect ',' after destination operand, but got: '%s'",
        CURRENT_LEXEME
    );

    Token *src_token = NULL;

    if(match(parser, 2, DWORD_TYPE_TOKEN_TYPE, REGISTER_TOKEN_TYPE)){
        src_token = previous(parser);
    }

    if(!src_token){
        error(
            parser,
            peek(parser),
            "Expect literal or register as source operand, but got: '%s'",
            CURRENT_LEXEME
        );
    }

    BinaryInstruction *instruction = MEMORY_NEW(
        ALLOCATOR,
        BinaryInstruction,
        token_to_location(parser, dst_token),
        token_to_location(parser, src_token),
        instruction_token,
        dst_token,
        src_token
    );

    return MEMORY_NEW(
        ALLOCATOR,
        Instruction,
        0,
        0,
        MOV_INSTRUCTION_TYPE,
        instruction
    );
}

Instruction *parse_pop_instruction(Parser *parser){
	Token *instruction_token = previous(parser);
    Token *dst_token = consume(
        parser,
        REGISTER_TOKEN_TYPE,
        "Expect register as destination operand, but got: '%s'",
        CURRENT_LEXEME
    );

    UnaryInstruction *instruction = MEMORY_NEW(
        ALLOCATOR,
        UnaryInstruction,
        token_to_location(parser, dst_token),
        instruction_token,
        dst_token,
    );

    return MEMORY_NEW(
        ALLOCATOR,
        Instruction,
        0,
        0,
        POP_INSTRUCTION_TYPE,
        instruction
    );
}

Instruction *parse_push_instruction(Parser *parser){
	Token *instruction_token = previous(parser);
    Token *src_token = consume(
        parser,
        REGISTER_TOKEN_TYPE,
        "Expect register as source operand, but got: '%s'",
        CURRENT_LEXEME
    );

    UnaryInstruction *instruction = MEMORY_NEW(
        ALLOCATOR,
        UnaryInstruction,
        token_to_location(parser, src_token),
        instruction_token,
        src_token,
    );

    return MEMORY_NEW(
        ALLOCATOR,
        Instruction,
        0,
        0,
        PUSH_INSTRUCTION_TYPE,
        instruction
    );
}

Instruction *parse_sub_instruction(Parser *parser){
	Token *instruction_token = previous(parser);
    Token *dst_token = consume(
        parser,
        REGISTER_TOKEN_TYPE,
        "Expect register, but got: '%s'",
        CURRENT_LEXEME
    );

    consume(
        parser,
        COMMA_TOKEN_TYPE,
        "Expect ',', but got: '%s'",
        CURRENT_LEXEME
    );

    Token *src_token = NULL;

    if(match(parser, 2, DWORD_TYPE_TOKEN_TYPE, REGISTER_TOKEN_TYPE)){
        src_token = previous(parser);
    }

    if(!src_token){
        error(
            parser,
            peek(parser),
            "Expect literal or register, but got: '%s'",
            CURRENT_LEXEME
        );
    }

    BinaryInstruction *instruction = MEMORY_NEW(
        ALLOCATOR,
        BinaryInstruction,
        token_to_location(parser, dst_token),
        token_to_location(parser, src_token),
        instruction_token,
        dst_token,
        src_token
    );

    return MEMORY_NEW(
        ALLOCATOR,
        Instruction,
        0,
        0,
        SUB_INSTRUCTION_TYPE,
        instruction
    );
}

Instruction *parse_ret_instruction(Parser *parser){
	EmptyInstruction *instruction = MEMORY_NEW(
        ALLOCATOR,
        EmptyInstruction,
        previous(parser)
    );

    return MEMORY_NEW(
        ALLOCATOR,
        Instruction,
        0,
        0,
        RET_INSTRUCTION_TYPE,
        instruction
    );
}

Instruction *parse_xor_instruction(Parser *parser){
	Token *instruction_token = previous(parser);
    Token *dst_token = consume(
        parser,
        REGISTER_TOKEN_TYPE,
        "Expect register, but got: '%s'",
        CURRENT_LEXEME
    );

    consume(
        parser,
        COMMA_TOKEN_TYPE,
        "Expect ',', but got: '%s'",
        CURRENT_LEXEME
    );

    Token *src_token = NULL;

    if(match(parser, 2, DWORD_TYPE_TOKEN_TYPE, REGISTER_TOKEN_TYPE)){
        src_token = previous(parser);
    }

    if(!src_token){
        error(
            parser,
            peek(parser),
            "Expect literal or register, but got: '%s'",
            CURRENT_LEXEME
        );
    }

    BinaryInstruction *instruction = MEMORY_NEW(
        ALLOCATOR,
        BinaryInstruction,
        token_to_location(parser, dst_token),
        token_to_location(parser, src_token),
        instruction_token,
        dst_token,
        src_token
    );

    return MEMORY_NEW(
        ALLOCATOR,
        Instruction,
        0,
        0,
        XOR_INSTRUCTION_TYPE,
        instruction
    );
}

Instruction *parse_instruction(Parser *parser){
    if(match(parser, 1, IDENTIFIER_TOKEN_TYPE)){
    	return parse_label_instruction(parser);
    }

    if(match(parser, 1, ADD_TOKEN_TYPE)){
    	return parse_add_instruction(parser);
    }

    if(match(parser, 1, CALL_TOKEN_TYPE)){
    	return parse_call_instruction(parser);
    }

    if(match(parser, 1, CMP_TOKEN_TYPE)){
        return parse_cmp_instruction(parser);
    }

    if(match(parser, 1, IDIV_TOKEN_TYPE)){
    	return parse_idiv_instruction(parser);
    }

    if(match(parser, 1, IMUL_TOKEN_TYPE)){
    	return parse_imul_instruction(parser);
    }

    if(match(
    	parser,
     	5,
      	JE_TOKEN_TYPE,
       	JG_TOKEN_TYPE,
        JL_TOKEN_TYPE,
        JGE_TOKEN_TYPE,
        JLE_TOKEN_TYPE
    )){
        return parse_jcc_instruction(parser);
    }

    if(match(parser, 1, JMP_TOKEN_TYPE)){
    	return parse_jmp_instruction(parser);
    }

    if(match(parser, 1, MOV_TOKEN_TYPE)){
    	return parse_mov_instruction(parser);
    }

    if(match(parser, 1, POP_TOKEN_TYPE)){
    	return parse_pop_instruction(parser);
    }

    if(match(parser, 1, PUSH_TOKEN_TYPE)){
    	return parse_push_instruction(parser);
    }

    if(match(parser, 1, SUB_TOKEN_TYPE)){
    	return parse_sub_instruction(parser);
    }

    if(match(parser, 1, RET_TOKEN_TYPE)){
        return parse_ret_instruction(parser);
    }

    if(match(parser, 1, XOR_TOKEN_TYPE)){
    	return parse_xor_instruction(parser);
    }

    error(
        parser,
        peek(parser),
        "Expect instruction, but got: '%s'",
        CURRENT_LEXEME
    );

    return NULL;
}
//------------------------------------------------------------
//                  PUBLIC IMPLEMENTATOIN                   //
//------------------------------------------------------------
Parser *parser_create(Allocator *allocator){
    Parser *parser = MEMORY_ALLOC(Parser, 1, allocator);

    if(!parser){
        return NULL;
    }

    parser->allocator = allocator;

    return parser;
}

void parser_destroy(Parser *parser){
    if(!parser){
        return;
    }

    MEMORY_DEALLOC(parser, Parser, 1, parser->allocator);
}

int parser_parse(Parser *parser, DynArr *tokens, DynArr *instructions){
    if(setjmp(parser->err_buf) == 0){
        parser->current = 0;
        parser->tokens = tokens;

        while(!is_at_end(parser)){
            Instruction *instruction = parse_instruction(parser);
            dynarr_insert_ptr(instruction, instructions);
        }

        return 0;
    }else{
        return 1;
    }
}
