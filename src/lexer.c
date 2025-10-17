#include "lexer.h"
#include "types.h"
#include "token.h"

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>

#define ALLOCATOR (lexer->allocator)

static int64_t decimal_str_to_i64(size_t str_len, const char *str);
static int is_digit(char c);
static int is_alpha(char c);
static int is_alpha_numeric(char c);

static void error(Lexer *lexer, const char *fmt, ...);
static int is_at_end(const Lexer *lexer);
static char previous(const Lexer *lexer);
static char advance(Lexer *lexer);
static char peek(const Lexer *lexer);
static int match(Lexer *lexer, char c);
static const char *code_slice(
	const Lexer *lexer,
	size_t start,
	size_t end,
	size_t *out_len
);
static char *clone_range_code_slice(
	const Lexer *lexer,
	size_t start,
	size_t end,
	size_t *out_len
);
static char *clone_current_lexeme(const Lexer *lexer, size_t *out_len);
static void add_token_raw_l(
    Lexer *lexer,
    size_t lexeme_len,
    const char *lexeme,
    size_t literal_size,
    TokenType type,
    const void *literal
);
static void add_token_raw_h(
	Lexer *lexer,
	size_t literal_size,
	TokenType type,
	const void *literal
);
static void add_token(Lexer *lexer, TokenType type);
static inline TokenType *get_keyword_type(
	const Lexer *lexer,
	size_t keyword_size,
	const char *keyword
);

int64_t decimal_str_to_i64(size_t str_len, const char *str){
    int64_t value = 0;
    int is_negative = str[0] == '-';

    for(size_t i = is_negative ? 1 : 0; i < str_len; i++){
        char c = str[i];
        int64_t digit = ((int64_t)c) - 48;

        value *= 10;
        value += digit;
    }

    if(is_negative == 1){
        value *= -1;
    }

    return value;
}

inline int is_digit(char c){
    return c >= '0' && c <= '9';
}

inline int is_alpha(char c){
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

inline int is_alpha_numeric(char c){
    return is_alpha(c) || is_digit(c) || c == '_';
}

void error(Lexer *lexer, const char *fmt, ...){
    va_list args;
    va_start(args, fmt);

    fprintf(stderr, "LEXER ERROR\n\t");
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n");

    va_end(args);

    longjmp(lexer->err_buf, 1);
}

inline int is_at_end(const Lexer *lexer){
    return ((size_t)lexer->current) >= lexer->code->len;
}

inline char previous(const Lexer *lexer){
    return lexer->code->buff[lexer->current - 1];
}

inline char advance(Lexer *lexer){
    if(is_at_end(lexer)){
        return '\0';
    }

    return lexer->code->buff[lexer->current++];
}

inline char peek(const Lexer *lexer){
    if(is_at_end(lexer)){
        return '\0';
    }

    return lexer->code->buff[lexer->current];
}

inline int match(Lexer *lexer, char c){
    if(peek(lexer) == c){
        advance(lexer);
        return 1;
    }

    return 0;
}

const char *code_slice(const Lexer *lexer, size_t start, size_t end, size_t *out_len){
    assert(end > start && "'start' must be less than 'end'");

    size_t len = end - start;

    if(out_len){
        *out_len = len;
    }

    return &(lexer->code->buff[start]);
}

char *clone_range_code_slice(const Lexer *lexer, size_t start, size_t end, size_t *out_len){
    assert(end > start && "'start' must be less than 'end'");

    size_t len = end - start;
    char *cloned_code_slice = MEMORY_ALLOC(char, len + 1, ALLOCATOR);

    memcpy(cloned_code_slice, lexer->code->buff + start, len);
    cloned_code_slice[len] = 0;

    if(out_len){
        *out_len = len;
    }

    return cloned_code_slice;
}

inline char *clone_current_lexeme(const Lexer *lexer, size_t *out_len){
    return clone_range_code_slice(lexer, lexer->start, lexer->current, out_len);
}

static void add_token_raw_l(
    Lexer *lexer,
    size_t lexeme_len,
    const char *lexeme,
    size_t literal_size,
    TokenType type,
    const void *literal
){
    char *cloned_lexeme = MEMORY_ALLOC(char, lexeme_len + 1, ALLOCATOR);

    memcpy(cloned_lexeme, lexeme, lexeme_len);
    cloned_lexeme[lexeme_len] = 0;

    Token *token = MEMORY_NEW(
        ALLOCATOR,
        Token,
        lexer->start_line,
        lexer->end_line,
        lexer->start - lexer->start_line_offset + 1,
        lexer->current - lexer->end_line_offset,
        lexer->start,
        lexer->current - 1,
        lexeme_len,
        literal_size,
        type,
        cloned_lexeme,
        literal
    );

    dynarr_insert_ptr(token, lexer->tokens);
}

inline void add_token_raw_h(
	Lexer *lexer,
	size_t literal_size,
	TokenType type,
	const void *literal
){
    size_t lexeme_len;
    const char *lexeme = code_slice(lexer, lexer->start, lexer->current, &lexeme_len);

    add_token_raw_l(lexer, lexeme_len, lexeme, literal_size, type, literal);
}

inline void add_token(Lexer *lexer, TokenType type){
    add_token_raw_h(lexer, 0, type, NULL);
}

TokenType *get_keyword_type(
	const Lexer *lexer,
	size_t keyword_size,
	const char *keyword
){
    void *value = NULL;

    if(lzohtable_lookup(keyword_size, keyword, lexer->registers_keywords, &value)){
        return value;
    }

    if(lzohtable_lookup(keyword_size, keyword, lexer->instructions_keywords, &value)){
        return value;
    }

    return NULL;
}

static void number(Lexer *lexer){
    while (is_digit(peek(lexer))){
        advance(lexer);
    }

    size_t slice_len;
    const char *slice = code_slice(lexer, lexer->start, lexer->current, &slice_len);
    int64_t literal = decimal_str_to_i64(slice_len, slice);

    if(literal < INT32_MIN){
        error(
            lexer,
            "Literal value out of range, must be bigger or equals to %"PRIu32", but got: %" PRIu64,
            INT32_MIN,
            literal
        );

        return;
    }

    if(literal > INT32_MAX){
        error(
            lexer,
            "Literal value out of range, must be less or equals to %"PRIu32", but got: %" PRIu64,
            INT32_MAX,
            literal
        );

        return;
    }

    add_token_raw_h(
    	lexer,
        sizeof(int32_t),
        DWORD_TYPE_TOKEN_TYPE,
        MEMORY_NEW(ALLOCATOR, int32_t, (int32_t)literal)
    );
}

static void identifier(Lexer *lexer){
    while(is_alpha_numeric(peek(lexer))){
        advance(lexer);
    }

    size_t slice_len;
    const char *slice = code_slice(lexer, lexer->start, lexer->current, &slice_len);
    TokenType *type = get_keyword_type(lexer, slice_len, slice);

    add_token(lexer, type ? *type : IDENTIFIER_TOKEN_TYPE);
}

static void lex(Lexer *lexer){
    const char c = advance(lexer);

    switch (c){
        case ' ':
        case '\t':
            break;
        case '\n':{
            lexer->end_line_offset = lexer->current;
            lexer->end_line++;
            break;
        }case ',':{
            add_token(lexer, COMMA_TOKEN_TYPE);
            break;
        }case '-':{
            number(lexer);
            break;
        }case ':':{
            add_token(lexer, COLON_TOKEN_TYPE);
            break;
        }default:{
            if(is_digit(c)){
                number(lexer);
                break;
            }else if(c == '.' || is_alpha(c)){
                identifier(lexer);
                break;
            }

            error(lexer, "Unknown token: '%c'(%d)", c, c);

            break;
        }
    }
}

Lexer *lexer_create(Allocator *allocator){
    Lexer *lexer = MEMORY_ALLOC(Lexer, 1, allocator);

    if(!lexer){
        return NULL;
    }

    lexer->allocator = allocator;

    return lexer;
}

void lexer_destroy(Lexer *lexer){
    if(!lexer){
        return;
    }

    MEMORY_DEALLOC(lexer, Lexer, 1, lexer->allocator);
}

int lexer_lex(
    Lexer *lexer,
    LZOHTable *registers_keywords,
    LZOHTable *instructions_keywords,
    BStr *code,
    DynArr *tokens
){
    if(setjmp(lexer->err_buf) == 0){
        lexer->start_line_offset = 0;
        lexer->end_line_offset = 0;
        lexer->start_line = 1;
        lexer->end_line = 1;
        lexer->start = 0;
        lexer->current = 0;
        lexer->code = code;
        lexer->registers_keywords = registers_keywords;
        lexer->instructions_keywords = instructions_keywords;
        lexer->tokens = tokens;

        while(!is_at_end(lexer)){
            lex(lexer);
            lexer->start_line_offset = lexer->end_line_offset;
            lexer->start_line = lexer->end_line;
            lexer->start = lexer->current;
        }

        add_token_raw_l(lexer, 3, "EOF", 0, EOF_TOKEN_TYPE, NULL);

        return 0;
    }else{
        return 1;
    }
}
