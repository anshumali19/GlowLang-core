CC = gcc
NVCC := $(shell command -v nvcc 2> /dev/null)

# No -mavx2/-msse4.2: the SIMD tensor kernels are compiled for AVX2+FMA
# individually and chosen at run time (CPUID), so one binary runs on any
# x86-64 CPU, and the same flags work on ARM64.
CFLAGS = -O2 -lm -pthread -lssl -lcrypto
CFLAGS_ASAN = -O1 -g -lm -pthread -lssl -lcrypto -fsanitize=address -fno-omit-frame-pointer
CFLAGS_TSAN = -O1 -g -lm -pthread -lssl -lcrypto -fsanitize=thread -fno-omit-frame-pointer

FLEX = flex
BISON = bison

SRC_DIR = src
TOOLS_DIR = tools/gdu
BIN_DIR = bin

GLOWLANG_BIN = $(BIN_DIR)/glowlang
GDU_BIN = $(BIN_DIR)/gdu

# Hand-written runtime sources (the lexer and parser are generated).
RUNTIME_SRC = $(SRC_DIR)/lsp.c $(SRC_DIR)/arena.c $(SRC_DIR)/jit.c $(SRC_DIR)/opt.c $(SRC_DIR)/compiler.c \
              $(SRC_DIR)/vm.c $(SRC_DIR)/gc.c $(SRC_DIR)/stdlib.c $(SRC_DIR)/bigint.c $(SRC_DIR)/regex.c $(SRC_DIR)/corelib.c $(SRC_DIR)/blake3.c
GENERATED_SRC = $(SRC_DIR)/lex.yy.c $(SRC_DIR)/glowlang.tab.c
ALL_SRC = $(GENERATED_SRC) $(RUNTIME_SRC)
DEPS = $(SRC_DIR)/glowlang.l $(SRC_DIR)/glowlang.y $(RUNTIME_SRC) $(wildcard $(SRC_DIR)/*.h)

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

define generate
	$(FLEX) -o $(SRC_DIR)/lex.yy.c $(SRC_DIR)/glowlang.l
	$(BISON) -Wnone -d -o $(SRC_DIR)/glowlang.tab.c $(SRC_DIR)/glowlang.y
endef

define clean-generated
	rm -f $(SRC_DIR)/lex.yy.c $(SRC_DIR)/glowlang.tab.c $(SRC_DIR)/glowlang.tab.h
endef

$(GLOWLANG_BIN): $(DEPS) $(CUDA_OBJ)
	mkdir -p $(BIN_DIR)
	$(generate)
	$(CC) $(ALL_SRC) $(CUDA_OBJ) -o $(GLOWLANG_BIN) $(CFLAGS) $(LDFLAGS)
	$(clean-generated)

# The test runner compares each test's output with tests/expected/.
test: $(GLOWLANG_BIN)
	GLOWLANG_BIN=./$(GLOWLANG_BIN) ./scripts/run-tests.sh

test-sanitized: $(DEPS)
	mkdir -p $(BIN_DIR)
	@echo "Building with AddressSanitizer..."
	$(generate)
	$(CC) $(ALL_SRC) -o $(BIN_DIR)/glowlang_asan $(CFLAGS_ASAN) $(LDFLAGS)
	@echo "Running tests with ASan..."
	ASAN_OPTIONS=detect_leaks=0:detect_stack_use_after_return=0 GLOWLANG_BIN=./$(BIN_DIR)/glowlang_asan ./scripts/run-tests.sh
	@echo "Building with ThreadSanitizer..."
	$(CC) $(ALL_SRC) -o $(BIN_DIR)/glowlang_tsan $(CFLAGS_TSAN) $(LDFLAGS)
	@echo "Running tests with TSan..."
	GLOWLANG_BIN=./$(BIN_DIR)/glowlang_tsan ./scripts/run-tests.sh
	$(clean-generated)

fuzz-parser: $(DEPS)
	mkdir -p $(BIN_DIR)
	$(generate)
	$(CC) tests/c_harness/fuzz_parser.c $(ALL_SRC) -o $(BIN_DIR)/fuzz_parser $(CFLAGS_ASAN) -DFUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION $(LDFLAGS)
	./$(BIN_DIR)/fuzz_parser
	$(clean-generated)

$(GDU_BIN): $(TOOLS_DIR)/gdu.c
	mkdir -p $(BIN_DIR)
	$(CC) $(TOOLS_DIR)/gdu.c -o $(GDU_BIN) $(CFLAGS) -lsqlite3

# Build and run a fuzz harness (call: $(call build-fuzz,harness_name))
define build-fuzz
	mkdir -p $(BIN_DIR)
	$(generate)
	$(CC) tests/c_harness/$(1).c $(ALL_SRC) -o $(BIN_DIR)/$(1) $(CFLAGS_ASAN) -DFUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION $(LDFLAGS)
	ASAN_OPTIONS=detect_leaks=0 ./$(BIN_DIR)/$(1)
	$(clean-generated)
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
verify: all test
	@echo "All tests passed."

clean:
	rm -f $(GLOWLANG_BIN) $(GDU_BIN) bin/glowlang.js bin/glowlang.wasm
	rm -f bin/fuzz_* bin/glow_profiler bin/glow_inspector bin/glow_debugger
	rm -f bin/glowlang_asan bin/glowlang_tsan

wasm: $(DEPS)
	mkdir -p $(BIN_DIR)
	$(generate)
	emcc $(ALL_SRC) -o $(BIN_DIR)/glowlang.js -O3 -s WASM=1 -s ALLOW_MEMORY_GROWTH=1 -s EXPORTED_RUNTIME_METHODS='["cwrap"]' -lm
	$(clean-generated)

.PHONY: all test test-sanitized fuzz-parser fuzz-bytecode fuzz-runtime fuzz-gc fuzz-string fuzz-module fuzz-all tools verify clean wasm
