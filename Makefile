CC = gcc
CFLAGS = -O2 -lm -pthread -lssl -lcrypto -mavx2 -msse4.2
CFLAGS_ASAN = -O1 -g -lm -pthread -lssl -lcrypto -mavx2 -msse4.2 -fsanitize=address -fno-omit-frame-pointer
CFLAGS_TSAN = -O1 -g -lm -pthread -lssl -lcrypto -mavx2 -msse4.2 -fsanitize=thread -fno-omit-frame-pointer

FLEX = flex
BISON = bison

SRC_DIR = src
TOOLS_DIR = tools/gdu
BIN_DIR = bin

GLOWLANG_BIN = $(BIN_DIR)/glowlang
GDU_BIN = $(BIN_DIR)/gdu

all: $(GLOWLANG_BIN) $(GDU_BIN)

$(GLOWLANG_BIN): $(SRC_DIR)/glowlang.l $(SRC_DIR)/glowlang.y $(SRC_DIR)/lsp.c $(SRC_DIR)/glowlang.h $(SRC_DIR)/lsp.h $(SRC_DIR)/arena.c $(SRC_DIR)/arena.h $(SRC_DIR)/jit.c $(SRC_DIR)/jit.h $(SRC_DIR)/opt.c $(SRC_DIR)/opt.h $(SRC_DIR)/compiler.c $(SRC_DIR)/vm.c $(SRC_DIR)/gc.c $(SRC_DIR)/stdlib.c
	$(FLEX) -o $(SRC_DIR)/lex.yy.c $(SRC_DIR)/glowlang.l
	$(BISON) -Wnone -d -o $(SRC_DIR)/glowlang.tab.c $(SRC_DIR)/glowlang.y
	$(CC) $(SRC_DIR)/lex.yy.c $(SRC_DIR)/glowlang.tab.c $(SRC_DIR)/lsp.c $(SRC_DIR)/arena.c $(SRC_DIR)/jit.c $(SRC_DIR)/opt.c $(SRC_DIR)/compiler.c $(SRC_DIR)/vm.c $(SRC_DIR)/gc.c $(SRC_DIR)/stdlib.c -o $(GLOWLANG_BIN) $(CFLAGS) -lfl -lsqlite3 -lX11
	rm -f $(SRC_DIR)/lex.yy.c $(SRC_DIR)/glowlang.tab.c $(SRC_DIR)/glowlang.tab.h

test-sanitized: $(SRC_DIR)/glowlang.l $(SRC_DIR)/glowlang.y $(SRC_DIR)/lsp.c $(SRC_DIR)/glowlang.h $(SRC_DIR)/lsp.h $(SRC_DIR)/arena.c $(SRC_DIR)/arena.h $(SRC_DIR)/jit.c $(SRC_DIR)/jit.h $(SRC_DIR)/opt.c $(SRC_DIR)/opt.h $(SRC_DIR)/compiler.c $(SRC_DIR)/vm.c $(SRC_DIR)/gc.c $(SRC_DIR)/stdlib.c
	@echo "Building with AddressSanitizer..."
	$(FLEX) -o $(SRC_DIR)/lex.yy.c $(SRC_DIR)/glowlang.l
	$(BISON) -Wnone -d -o $(SRC_DIR)/glowlang.tab.c $(SRC_DIR)/glowlang.y
	$(CC) $(SRC_DIR)/lex.yy.c $(SRC_DIR)/glowlang.tab.c $(SRC_DIR)/lsp.c $(SRC_DIR)/arena.c $(SRC_DIR)/jit.c $(SRC_DIR)/opt.c $(SRC_DIR)/compiler.c $(SRC_DIR)/vm.c $(SRC_DIR)/gc.c $(SRC_DIR)/stdlib.c -o $(BIN_DIR)/glowlang_asan $(CFLAGS_ASAN) -lfl -lsqlite3 -lX11
	@echo "Running tests with ASan..."
	GLOWLANG_BIN=./bin/glowlang_asan ./run_all_tests.sh
	@echo "Building with ThreadSanitizer..."
	$(CC) $(SRC_DIR)/lex.yy.c $(SRC_DIR)/glowlang.tab.c $(SRC_DIR)/lsp.c $(SRC_DIR)/arena.c $(SRC_DIR)/jit.c $(SRC_DIR)/opt.c $(SRC_DIR)/compiler.c $(SRC_DIR)/vm.c $(SRC_DIR)/gc.c $(SRC_DIR)/stdlib.c -o $(BIN_DIR)/glowlang_tsan $(CFLAGS_TSAN) -lfl -lsqlite3 -lX11
	@echo "Running tests with TSan..."
	GLOWLANG_BIN=./bin/glowlang_tsan ./run_all_tests.sh
	rm -f $(SRC_DIR)/lex.yy.c $(SRC_DIR)/glowlang.tab.c $(SRC_DIR)/glowlang.tab.h

fuzz-parser: $(SRC_DIR)/glowlang.l $(SRC_DIR)/glowlang.y $(SRC_DIR)/lsp.c $(SRC_DIR)/glowlang.h $(SRC_DIR)/lsp.h $(SRC_DIR)/arena.c $(SRC_DIR)/arena.h $(SRC_DIR)/jit.c $(SRC_DIR)/jit.h $(SRC_DIR)/opt.c $(SRC_DIR)/opt.h $(SRC_DIR)/compiler.c $(SRC_DIR)/vm.c $(SRC_DIR)/gc.c $(SRC_DIR)/stdlib.c
	$(FLEX) -o $(SRC_DIR)/lex.yy.c $(SRC_DIR)/glowlang.l
	$(BISON) -Wnone -d -o $(SRC_DIR)/glowlang.tab.c $(SRC_DIR)/glowlang.y
	$(CC) tests/c_harness/fuzz_parser.c $(SRC_DIR)/lex.yy.c $(SRC_DIR)/glowlang.tab.c $(SRC_DIR)/lsp.c $(SRC_DIR)/arena.c $(SRC_DIR)/jit.c $(SRC_DIR)/opt.c $(SRC_DIR)/compiler.c $(SRC_DIR)/vm.c $(SRC_DIR)/gc.c $(SRC_DIR)/stdlib.c -o $(BIN_DIR)/fuzz_parser $(CFLAGS_ASAN) -DFUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION -lfl -lsqlite3 -lX11
	./$(BIN_DIR)/fuzz_parser
	rm -f $(SRC_DIR)/lex.yy.c $(SRC_DIR)/glowlang.tab.c $(SRC_DIR)/glowlang.tab.h

$(GDU_BIN): $(TOOLS_DIR)/gdu.c
	$(CC) $(TOOLS_DIR)/gdu.c -o $(GDU_BIN) $(CFLAGS) -lsqlite3

clean:
	rm -f $(GLOWLANG_BIN) $(GDU_BIN) bin/glowlang.js bin/glowlang.wasm

wasm: $(SRC_DIR)/glowlang.l $(SRC_DIR)/glowlang.y $(SRC_DIR)/lsp.c $(SRC_DIR)/glowlang.h $(SRC_DIR)/lsp.h $(SRC_DIR)/arena.c $(SRC_DIR)/arena.h $(SRC_DIR)/jit.c $(SRC_DIR)/jit.h $(SRC_DIR)/opt.c $(SRC_DIR)/opt.h $(SRC_DIR)/compiler.c $(SRC_DIR)/vm.c $(SRC_DIR)/gc.c $(SRC_DIR)/stdlib.c
	$(FLEX) -o $(SRC_DIR)/lex.yy.c $(SRC_DIR)/glowlang.l
	$(BISON) -Wnone -d -o $(SRC_DIR)/glowlang.tab.c $(SRC_DIR)/glowlang.y
	emcc $(SRC_DIR)/lex.yy.c $(SRC_DIR)/glowlang.tab.c $(SRC_DIR)/lsp.c $(SRC_DIR)/arena.c $(SRC_DIR)/jit.c $(SRC_DIR)/opt.c $(SRC_DIR)/compiler.c $(SRC_DIR)/vm.c $(SRC_DIR)/gc.c $(SRC_DIR)/stdlib.c -o $(BIN_DIR)/glowlang.js -O3 -s WASM=1 -s ALLOW_MEMORY_GROWTH=1 -s EXPORTED_RUNTIME_METHODS='["cwrap"]' -lm
	rm -f $(SRC_DIR)/lex.yy.c $(SRC_DIR)/glowlang.tab.c $(SRC_DIR)/glowlang.tab.h
