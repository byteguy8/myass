#include "essentials/lzarena.h"
#include "myass.h"

#include <stdio.h>
#include <string.h>
#include <inttypes.h>

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
    if(argc != 2){
        fprintf(stderr, "Usage: myass <source file>\n");
        exit(EXIT_FAILURE);
    }

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

    BStr *input = read_source(&allocator, argv[1]);
    MyAss *myass = myass_create(&allocator);

    myass_assemble(myass, input->len, input->buff);
    myass_print_as_hex(myass, 0);

    lzarena_destroy(arena);

    return 0;
}