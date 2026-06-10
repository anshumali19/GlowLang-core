CC = gcc
NVCC := $(shell command -v nvcc 2> /dev/null)

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

LDFLAGS = -lsqlite3 -lX11
CUDA_OBJ =

ifdef NVCC
CFLAGS += -DGLOW_USE_CUDA
LDFLAGS += -lcudart -lcublas
CUDA_OBJ = $(SRC_DIR)/cuda/tensor_ops.o

$(SRC_DIR)/cuda/tensor_ops.o: $(SRC_DIR)/cuda/tensor_ops.cu
	mkdir -p $(SRC_DIR)/cuda
	$(NVCC) -c $(SRC_DIR)/cuda/tensor_ops.cu -o $(SRC_DIR)/cuda/tensor_ops.o -O2
endif

$(GLOWLANG_BIN): $(SRC_DIR)/glowlang.l $(SRC_DIR)/glowlang.y $(SRC_DIR)/lsp.c $(SRC_DIR)/glowlang.h $(SRC_DIR)/lsp.h $(SRC_DIR)/arena.c $(SRC_DIR)/arena.h $(SRC_DIR)/jit.c $(SRC_DIR)/jit.h $(SRC_DIR)/opt.c $(SRC_DIR)/opt.h $(SRC_DIR)/compiler.c $(SRC_DIR)/vm.c $(SRC_DIR)/gc.c $(SRC_DIR)/stdlib.c $(CUDA_OBJ)
	$(FLEX) -o $(SRC_DIR)/lex.yy.c $(SRC_DIR)/glowlang.l
	$(BISON) -Wnone -d -o $(SRC_DIR)/glowlang.tab.c $(SRC_DIR)/glowlang.y
	$(CC) $(SRC_DIR)/lex.yy.c $(SRC_DIR)/glowlang.tab.c $(SRC_DIR)/lsp.c $(SRC_DIR)/arena.c $(SRC_DIR)/jit.c $(SRC_DIR)/opt.c $(SRC_DIR)/compiler.c $(SRC_DIR)/vm.c $(SRC_DIR)/gc.c $(SRC_DIR)/stdlib.c $(CUDA_OBJ) -o $(GLOWLANG_BIN) $(CFLAGS) $(LDFLAGS)
	rm -f $(SRC_DIR)/lex.yy.c $(SRC_DIR)/glowlang.tab.c $(SRC_DIR)/glowlang.tab.h

test-sanitized: $(SRC_DIR)/glowlang.l $(SRC_DIR)/glowlang.y $(SRC_DIR)/lsp.c $(SRC_DIR)/glowlang.h $(SRC_DIR)/lsp.h $(SRC_DIR)/arena.c $(SRC_DIR)/arena.h $(SRC_DIR)/jit.c $(SRC_DIR)/jit.h $(SRC_DIR)/opt.c $(SRC_DIR)/opt.h $(SRC_DIR)/compiler.c $(SRC_DIR)/vm.c $(SRC_DIR)/gc.c $(SRC_DIR)/stdlib.c
	@echo "Building with AddressSanitizer..."
	$(FLEX) -o $(SRC_DIR)/lex.yy.c $(SRC_DIR)/glowlang.l
	$(BISON) -Wnone -d -o $(SRC_DIR)/glowlang.tab.c $(SRC_DIR)/glowlang.y
	$(CC) $(SRC_DIR)/lex.yy.c $(SRC_DIR)/glowlang.tab.c $(SRC_DIR)/lsp.c $(SRC_DIR)/arena.c $(SRC_DIR)/jit.c $(SRC_DIR)/opt.c $(SRC_DIR)/compiler.c $(SRC_DIR)/vm.c $(SRC_DIR)/gc.c $(SRC_DIR)/stdlib.c -o $(BIN_DIR)/glowlang_asan $(CFLAGS_ASAN) -lsqlite3 -lX11
	@echo "Running tests with ASan..."
	GLOWLANG_BIN=./bin/glowlang_asan ./run_all_tests.sh
	@echo "Building with ThreadSanitizer..."
	$(CC) $(SRC_DIR)/lex.yy.c $(SRC_DIR)/glowlang.tab.c $(SRC_DIR)/lsp.c $(SRC_DIR)/arena.c $(SRC_DIR)/jit.c $(SRC_DIR)/opt.c $(SRC_DIR)/compiler.c $(SRC_DIR)/vm.c $(SRC_DIR)/gc.c $(SRC_DIR)/stdlib.c -o $(BIN_DIR)/glowlang_tsan $(CFLAGS_TSAN) -lsqlite3 -lX11
	@echo "Running tests with TSan..."
	GLOWLANG_BIN=./bin/glowlang_tsan ./run_all_tests.sh
	rm -f $(SRC_DIR)/lex.yy.c $(SRC_DIR)/glowlang.tab.c $(SRC_DIR)/glowlang.tab.h

fuzz-parser: $(SRC_DIR)/glowlang.l $(SRC_DIR)/glowlang.y $(SRC_DIR)/lsp.c $(SRC_DIR)/glowlang.h $(SRC_DIR)/lsp.h $(SRC_DIR)/arena.c $(SRC_DIR)/arena.h $(SRC_DIR)/jit.c $(SRC_DIR)/jit.h $(SRC_DIR)/opt.c $(SRC_DIR)/opt.h $(SRC_DIR)/compiler.c $(SRC_DIR)/vm.c $(SRC_DIR)/gc.c $(SRC_DIR)/stdlib.c
	$(FLEX) -o $(SRC_DIR)/lex.yy.c $(SRC_DIR)/glowlang.l
	$(BISON) -Wnone -d -o $(SRC_DIR)/glowlang.tab.c $(SRC_DIR)/glowlang.y
	$(CC) tests/c_harness/fuzz_parser.c $(SRC_DIR)/lex.yy.c $(SRC_DIR)/glowlang.tab.c $(SRC_DIR)/lsp.c $(SRC_DIR)/arena.c $(SRC_DIR)/jit.c $(SRC_DIR)/opt.c $(SRC_DIR)/compiler.c $(SRC_DIR)/vm.c $(SRC_DIR)/gc.c $(SRC_DIR)/stdlib.c -o $(BIN_DIR)/fuzz_parser $(CFLAGS_ASAN) -DFUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION -lsqlite3 -lX11
	./$(BIN_DIR)/fuzz_parser
	rm -f $(SRC_DIR)/lex.yy.c $(SRC_DIR)/glowlang.tab.c $(SRC_DIR)/glowlang.tab.h

$(GDU_BIN): $(TOOLS_DIR)/gdu.c
	$(CC) $(TOOLS_DIR)/gdu.c -o $(GDU_BIN) $(CFLAGS) -lsqlite3

FUZZ_SRC = $(SRC_DIR)/lex.yy.c $(SRC_DIR)/glowlang.tab.c $(SRC_DIR)/lsp.c $(SRC_DIR)/arena.c $(SRC_DIR)/jit.c $(SRC_DIR)/opt.c $(SRC_DIR)/compiler.c $(SRC_DIR)/vm.c $(SRC_DIR)/gc.c $(SRC_DIR)/stdlib.c
FUZZ_LIBS = -lsqlite3 -lX11

# Build a fuzz harness helper (call: $(call build-fuzz,harness_name))
define build-fuzz
	$(FLEX) -o $(SRC_DIR)/lex.yy.c $(SRC_DIR)/glowlang.l
	$(BISON) -Wnone -d -o $(SRC_DIR)/glowlang.tab.c $(SRC_DIR)/glowlang.y
	$(CC) tests/c_harness/$(1).c $(FUZZ_SRC) -o $(BIN_DIR)/$(1) $(CFLAGS_ASAN) -DFUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION $(FUZZ_LIBS)
	./$(BIN_DIR)/$(1)
	rm -f $(SRC_DIR)/lex.yy.c $(SRC_DIR)/glowlang.tab.c $(SRC_DIR)/glowlang.tab.h
endef

fuzz-bytecode:
	$(call build-fuzz,fuzz_bytecode)

fuzz-runtime:
	$(call build-fuzz,fuzz_runtime)

fuzz-gc:
	$(call build-fuzz,fuzz_gc)

fuzz-string:
	$(call build-fuzz,fuzz_string)

fuzz-module:
	$(call build-fuzz,fuzz_module)

fuzz-all: fuzz-parser fuzz-bytecode fuzz-runtime fuzz-gc fuzz-string fuzz-module
	@echo "All fuzzers completed successfully."

# Standalone tools
tools: $(GDU_BIN)
	$(CC) tools/profiler/glow_profiler.c -o $(BIN_DIR)/glow_profiler -O2
	$(CC) tools/inspector/glow_inspector.c -o $(BIN_DIR)/glow_inspector -O2
	$(CC) tools/debugger/glow_debugger.c -o $(BIN_DIR)/glow_debugger -O2
	@echo "All tools built successfully."

# Full verification pipeline
verify: all
	./run_all_tests.sh
	@echo "All tests passed."

clean:
	rm -f $(GLOWLANG_BIN) $(GDU_BIN) bin/glowlang.js bin/glowlang.wasm
	rm -f bin/fuzz_* bin/glow_profiler bin/glow_inspector bin/glow_debugger
	rm -f bin/glowlang_asan bin/glowlang_tsan

wasm: $(SRC_DIR)/glowlang.l $(SRC_DIR)/glowlang.y $(SRC_DIR)/lsp.c $(SRC_DIR)/glowlang.h $(SRC_DIR)/lsp.h $(SRC_DIR)/arena.c $(SRC_DIR)/arena.h $(SRC_DIR)/jit.c $(SRC_DIR)/jit.h $(SRC_DIR)/opt.c $(SRC_DIR)/opt.h $(SRC_DIR)/compiler.c $(SRC_DIR)/vm.c $(SRC_DIR)/gc.c $(SRC_DIR)/stdlib.c
	$(FLEX) -o $(SRC_DIR)/lex.yy.c $(SRC_DIR)/glowlang.l
	$(BISON) -Wnone -d -o $(SRC_DIR)/glowlang.tab.c $(SRC_DIR)/glowlang.y
	emcc $(SRC_DIR)/lex.yy.c $(SRC_DIR)/glowlang.tab.c $(SRC_DIR)/lsp.c $(SRC_DIR)/arena.c $(SRC_DIR)/jit.c $(SRC_DIR)/opt.c $(SRC_DIR)/compiler.c $(SRC_DIR)/vm.c $(SRC_DIR)/gc.c $(SRC_DIR)/stdlib.c -o $(BIN_DIR)/glowlang.js -O3 -s WASM=1 -s ALLOW_MEMORY_GROWTH=1 -s EXPORTED_RUNTIME_METHODS='["cwrap"]' -lm
	rm -f $(SRC_DIR)/lex.yy.c $(SRC_DIR)/glowlang.tab.c $(SRC_DIR)/glowlang.tab.h
