#include "essentials/lzarena.h"
#include "myass.h"
#include "types.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>

#define ARG_FORMATTED_PRINT 0b00000001

typedef struct args{
	byte flags;
	const char *input;
}Args;

Args parse_args(int argc, char const *argv[]){
	byte flags = 0;
	const char *input = NULL;

	for (int i = 0; i < argc; i++) {
		const char *arg = argv[i];
		size_t arg_len = strlen(arg);

		if(arg_len == 2 && (strncmp(arg, "-f", 2) == 0)){
			flags |= ARG_FORMATTED_PRINT;
		}else{
			input = arg;
		}
	}

	return (Args){.flags = flags, .input = input};
}

BStr *read_source(const Allocator *allocator, const char *pathname){
	FILE *source_file = fopen(pathname, "r");

    if(!source_file){
        fprintf(
            stderr,
            "Failed to open pathname: '%s'. Check if exists or read permision",
            pathname
        );
        exit(EXIT_FAILURE);

        return NULL;
    }

	fseek(source_file, 0, SEEK_END);

	size_t source_size = (size_t)ftell(source_file);
    char *buff = MEMORY_ALLOC(char, source_size + 1, allocator);
	BStr *rstr = MEMORY_ALLOC(BStr, 1, allocator);

	fseek(source_file, 0, SEEK_SET);
	fread(buff, 1, source_size, source_file);
	fclose(source_file);

	buff[source_size] = '\0';

	rstr->len = source_size;
	rstr->buff = buff;

	return rstr;
}

int main(int argc, char const *argv[]){
    if(argc < 2){
        fprintf(stderr, "Usage: myass <source file>\n");
        fprintf(stderr, "\n");
        fprintf(stderr, "Arguments\n");
        fprintf(stderr, "  -f\n");
        fprintf(stderr, "                      Format output\n");

        exit(EXIT_FAILURE);
    }

    Args args = parse_args(argc, argv);
    LZArena *arena = lzarena_create(NULL);
    AllocatorContext allocator_context = {
        .err_buf = NULL,
        .behind_allocator = arena
    };
    Allocator allocator = {0};

    MEMORY_INIT_ALLOCATOR(
        &allocator_context,
        memory_arena_alloc,
        memory_arena_realloc,
        memory_arena_dealloc,
        &allocator
    );

    BStr *input = read_source(&allocator, args.input);
    MyAss *myass = myass_create(&allocator);

    myass_assemble(myass, input->len, input->buff);

    if(args.flags & ARG_FORMATTED_PRINT){
   		myass_formatted_print_hex(myass);
    }else{
    	myass_print_as_hex(myass, 0);
    }

    lzarena_destroy(arena);

    return 0;
}
