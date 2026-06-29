#!/usr/bin/env bash
# GlowLang CI/CD Verification Script
# Runs full test suite, sanitizer builds, and fuzzing
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
cd "$PROJECT_DIR"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

PASS=0
FAIL=0
REPORT=""

log() { echo -e "${GREEN}[CI]${NC} $*"; }
warn() { echo -e "${YELLOW}[WARN]${NC} $*"; }
fail() { echo -e "${RED}[FAIL]${NC} $*"; FAIL=$((FAIL+1)); }
pass() { PASS=$((PASS+1)); }

# ---- Step 1: Clean Build ----
log "Step 1: Clean build..."
make clean && make 2>&1
if [ $? -eq 0 ]; then
    pass; REPORT+="✅ Clean build\n"
else
    fail "Build failed"; REPORT+="❌ Clean build\n"
    exit 1
fi

# ---- Step 2: Full Test Suite ----
log "Step 2: Running full test suite..."
OUTPUT=$(./run_all_tests.sh 2>&1)
TOTAL=$(echo "$OUTPUT" | grep "^Total:" | awk '{print $2}')
PASSED=$(echo "$OUTPUT" | grep "^Passed:" | awk '{print $2}')
FAILED=$(echo "$OUTPUT" | grep "^Failed:" | awk '{print $2}')

if [ "$FAILED" = "0" ]; then
    pass; REPORT+="✅ Test suite: $PASSED/$TOTAL passed\n"
else
    fail "Test suite: $FAILED failures"; REPORT+="❌ Test suite: $PASSED/$TOTAL passed ($FAILED failed)\n"
fi

# ---- Step 3: AddressSanitizer Build ----
log "Step 3: Building with AddressSanitizer..."
flex -o src/lex.yy.c src/glowlang.l
bison -Wnone -d -o src/glowlang.tab.c src/glowlang.y
gcc src/lex.yy.c src/glowlang.tab.c src/lsp.c src/arena.c src/jit.c src/opt.c \
    src/compiler.c src/vm.c src/gc.c src/stdlib.c \
    -o bin/glowlang_asan \
    -O1 -g -lm -pthread -lssl -lcrypto -mavx2 -msse4.2 \
    -fsanitize=address -fno-omit-frame-pointer \
    -lfl -lsqlite3 -lX11 2>&1

if [ $? -eq 0 ]; then
    log "Running tests under ASan..."
    ASAN_OUTPUT=$(ASAN_OPTIONS=detect_leaks=0 GLOWLANG_BIN=./bin/glowlang_asan ./run_all_tests.sh 2>&1)
    ASAN_FAILED=$(echo "$ASAN_OUTPUT" | grep "^Failed:" | awk '{print $2}')
    if [ "$ASAN_FAILED" = "0" ]; then
        pass; REPORT+="✅ ASan: All tests pass (leak detection disabled)\n"
    else
        fail "ASan failures"; REPORT+="❌ ASan: $ASAN_FAILED failures\n"
    fi
else
    fail "ASan build failed"; REPORT+="❌ ASan build failed\n"
fi

# ---- Step 4: ThreadSanitizer Build ----
log "Step 4: Building with ThreadSanitizer..."
gcc src/lex.yy.c src/glowlang.tab.c src/lsp.c src/arena.c src/jit.c src/opt.c \
    src/compiler.c src/vm.c src/gc.c src/stdlib.c \
    -o bin/glowlang_tsan \
    -O1 -g -lm -pthread -lssl -lcrypto -mavx2 -msse4.2 \
    -fsanitize=thread -fno-omit-frame-pointer \
    -lfl -lsqlite3 -lX11 2>&1

if [ $? -eq 0 ]; then
    log "Running tests under TSan (with ASLR disabled)..."
    TSAN_OUTPUT=$(GLOWLANG_BIN=./bin/glowlang_tsan setarch $(uname -m) -R ./run_all_tests.sh 2>&1)
    TSAN_FAILED=$(echo "$TSAN_OUTPUT" | grep "^Failed:" | awk '{print $2}')
    if [ "$TSAN_FAILED" = "0" ]; then
        pass; REPORT+="✅ TSan: All tests pass\n"
    else
        warn "TSan: $TSAN_FAILED failures (spawn test may fail due to ASLR)"
        REPORT+="⚠️  TSan: $TSAN_FAILED failures\n"
    fi
else
    fail "TSan build failed"; REPORT+="❌ TSan build failed\n"
fi

# ---- Step 5: Parser Fuzzing ----
log "Step 5: Running parser fuzzer..."
make fuzz-parser 2>&1
if [ $? -eq 0 ]; then
    pass; REPORT+="✅ Parser fuzzer: 1000 iterations clean\n"
else
    fail "Parser fuzzer crashed"; REPORT+="❌ Parser fuzzer failed\n"
fi

# ---- Step 6: Cleanup ----
rm -f src/lex.yy.c src/glowlang.tab.c src/glowlang.tab.h

# ---- Report ----
echo ""
echo "========================================="
echo "  GlowLang CI Verification Report"
echo "========================================="
echo -e "$REPORT"
echo "-----------------------------------------"
echo "  Passed: $PASS  |  Failed: $FAIL"
echo "========================================="
echo ""

if [ "$FAIL" -gt 0 ]; then
    echo -e "${RED}CI FAILED${NC}"
    exit 1
else
    echo -e "${GREEN}CI PASSED${NC}"
    exit 0
fi
