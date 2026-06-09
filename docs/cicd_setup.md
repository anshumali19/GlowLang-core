# GlowLang CI/CD Setup Guide

## GitHub Actions

Create `.github/workflows/ci.yml` (included in this repo):

```yaml
name: GlowLang CI
on:
  push:
    branches: [ main, develop ]
  pull_request:
    branches: [ main ]

jobs:
  build-and-test:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - name: Install dependencies
        run: sudo apt-get install -y flex bison libfl-dev libsqlite3-dev libssl-dev libx11-dev
      - name: Build
        run: make clean && make
      - name: Test
        run: ./run_all_tests.sh
      - name: ASan build
        run: |
          flex -o src/lex.yy.c src/glowlang.l
          bison -Wnone -d -o src/glowlang.tab.c src/glowlang.y
          gcc src/lex.yy.c src/glowlang.tab.c src/lsp.c src/arena.c src/jit.c \
              src/opt.c src/compiler.c src/vm.c src/gc.c src/stdlib.c \
              -o bin/glowlang_asan -O1 -g -fsanitize=address \
              -lm -pthread -lssl -lcrypto -mavx2 -msse4.2 -lfl -lsqlite3 -lX11
      - name: Test under ASan
        run: ASAN_OPTIONS=detect_leaks=0 GLOWLANG_BIN=./bin/glowlang_asan ./run_all_tests.sh
      - name: Fuzz
        run: make fuzz-parser
```

## Local CI

```bash
chmod +x scripts/ci.sh
./scripts/ci.sh
```

This runs:
1. Clean build
2. Full test suite (94 tests)
3. AddressSanitizer build + tests
4. ThreadSanitizer build + tests (with ASLR workaround)
5. Parser fuzzing (1000 iterations)

## Sanitizer Notes

- **ASan**: Detects memory errors. Run with `ASAN_OPTIONS=detect_leaks=0` (GlowLang uses arena allocation).
- **TSan**: Detects data races. Requires `setarch $(uname -m) -R` on kernel >= 6.6 due to ASLR.

## Release Checklist

1. All 94 tests pass
2. ASan build: no errors
3. TSan build: no data races
4. Parser fuzzer: 1000 iterations clean
5. `gdu (Glow Dependency Utility) verify`: all packages pass SHA-256
6. Documentation updated
