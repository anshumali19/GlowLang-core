#!/usr/bin/env bash
# Build GlowLang and run the whole test suite in the five modes every change
# must pass (CI runs the first four; the fifth catches GC bugs they miss):
#   1. compiled (JIT on)
#   2. interpreter only        GLOW_NOJITEXEC=1
#   3. GC stress               GLOWLANG_GC_STRESS=1 (collects at every allocation)
#   4. AddressSanitizer        a -fsanitize=address build
#   5. GC verify + stress + ASan
#
# Run it on Linux or WSL from a checkout with LF line endings (on a Windows
# checkout, see AGENTS.md: archive with core.autocrlf=false and build that).
#   scripts/test-all-modes.sh            prints one "Total:" line per mode
# Needs gcc, make, flex, bison, libssl-dev, libsqlite3-dev, libx11-dev.
set -u
cd "$(dirname "$0")/.."
make -j8 >/dev/null 2>&1 || { echo "make failed; run make to see why" >&2; exit 2; }
flex -o src/lex.yy.c src/glowlang.l && bison -Wnone -d -o src/glowlang.tab.c src/glowlang.y
gcc src/lex.yy.c src/glowlang.tab.c src/lsp.c src/arena.c src/jit.c src/opt.c src/compiler.c src/vm.c \
    src/gc.c src/stdlib.c src/bigint.c src/regex.c src/corelib.c src/process.c src/ffi.c src/datetime.c \
    src/blake3.c src/websocket.c -o bin/glowlang_asan -O1 -g -lm -pthread -lssl -lcrypto \
    -fsanitize=address -fno-omit-frame-pointer -lfl -lsqlite3 -lX11 -ldl 2>/dev/null \
    || { echo "the ASan build failed" >&2; exit 2; }
asan="ASAN_OPTIONS=detect_leaks=0:detect_stack_use_after_return=0 TEST_TIMEOUT=300 GLOWLANG_BIN=./bin/glowlang_asan"
failed=0
for m in "GLOWLANG_BIN=./bin/glowlang" \
         "GLOW_NOJITEXEC=1 GLOWLANG_BIN=./bin/glowlang" \
         "GLOWLANG_GC_STRESS=1 GLOWLANG_BIN=./bin/glowlang" \
         "$asan" \
         "GLOW_GC_VERIFY=1 GLOWLANG_GC_STRESS=1 $asan"; do
    echo "--- $m" | cut -c1-70
    out=$(env $m ./scripts/run-tests.sh 2>&1)
    echo "$out" | grep -E "Total|FAIL"
    echo "$out" | grep -q "Failed: 0" || failed=1
done
exit $failed
