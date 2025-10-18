PLATFORM         := LINUX
BUILD            := DEBUG

COMPILER.LINUX   := gcc
COMPILER.WINDOWS := mingw64
COMPILER         := $(COMPILER.$(PLATFORM))

FLAGS.WNOS       := -Wno-unused-variable -Wno-unused-parameter -Wno-unused-result -Wno-unused-function
FLAGS.LINUX      := -fsanitize=address,undefined,alignment
FLAGS.WINDOWS    :=
FLAGS.DEFAULT    := -Wall -Wextra -Iinclude -Iinclude/essentials
FLAGS.DEBUG      := -O0 -g2 $(FLAGS.$(PLATFORM))
FLAGS.RELEASE    := -O3 $(FLAGS.WNOS)
FLAGS            := $(FLAGS.DEFAULT) $(FLAGS.$(BUILD))

OUT_DIR          := build
SRC_DIR          := src

OBJS             := lzbstr.o dynarr.o lzstack.o lzohtable.o memory.o lzbbuff.o lzarena.o \
                    lexer.o parser.o myass.o

main: $(OBJS)
	$(COMPILER) -o build/main $(FLAGS) src/main.c build/*.o
myass.o:
	$(COMPILER) -c -o build/myass.o $(FLAGS) src/myass.c

parser.o:
	$(COMPILER) -c -o build/parser.o $(FLAGS) src/parser.c
lexer.o:
	$(COMPILER) -c -o build/lexer.o $(FLAGS) src/lexer.c

memory.o:
	$(COMPILER) -c -o $(OUT_DIR)/memory.o $(FLAGS) $(SRC_DIR)/essentials/memory.c
lzarena.o:
	$(COMPILER) -c -o $(OUT_DIR)/lzarena.o $(FLAGS) $(SRC_DIR)/essentials/lzarena.c
lzbbuff.o:
	$(COMPILER) -c -o $(OUT_DIR)/lzbbuff.o $(FLAGS) $(SRC_DIR)/essentials/lzbbuff.c
lzohtable.o:
	$(COMPILER) -c -o $(OUT_DIR)/lzohtable.o $(FLAGS) $(SRC_DIR)/essentials/lzohtable.c
lzstack.o:
	$(COMPILER) -c -o $(OUT_DIR)/lzstack.o $(FLAGS) $(SRC_DIR)/essentials/lzstack.c
dynarr.o:
	$(COMPILER) -c -o $(OUT_DIR)/dynarr.o $(FLAGS) $(SRC_DIR)/essentials/dynarr.c
lzbstr.o:
	$(COMPILER) -c -o $(OUT_DIR)/lzbstr.o $(FLAGS) $(SRC_DIR)/essentials/lzbstr.c
