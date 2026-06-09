# GlowLang Integration Testing Guide

## Running the Test Suite

```bash
./run_all_tests.sh
```

This runs all 94 `.glow` files in `tests/`. Each test prints output and returns exit code 0 on success.

## Writing Tests

Create a `.glow` file in `tests/`:

```glow
# tests/test_my_feature.glow

# Test basic functionality
x = 42
when x == 42:
    show "PASS: basic assignment"
otherwise:
    show "FAIL: basic assignment"
end

# Test with try/catch
try:
    result = my_function()
    show "PASS: function works"
catch e:
    show "FAIL: " + e
end
```

## Test Categories

### Core Language
- `test_class.glow` — Classes, blueprints, methods, inheritance
- `test_trait.glow` — Trait definitions and `can` usage
- `test_lambda.glow` — Closures, lambdas, higher-order functions
- `test_trycatch.glow` — Exception handling
- `test_natural.glow` — Natural language syntax (`whenever`, `every`, etc.)

### Data Structures
- `test_basic_list.glow` — List creation, push, pop, sort, reverse
- `test_dict_methods.glow` — Dict keys, values, has, remove, merge
- `test_fstring.glow` — F-string interpolation
- `listcomp_test.glow` — List comprehensions

### Runtime
- `test_spawn.glow` — Concurrency with `spawn`
- `test_gc.glow` — Garbage collection under pressure
- `test_profiler.glow` — Profiler operations

### I/O & Integration
- `test_database.glow`, `test_orm.glow` — SQLite ORM
- `test_http.glow`, `test_http_router.glow` — HTTP routing
- `test_json.glow`, `test_json_full.glow` — JSON parse/stringify
- `test_fs.glow`, `test_fs_full.glow` — Filesystem operations

## Sanitizer Testing

```bash
# AddressSanitizer
ASAN_OPTIONS=detect_leaks=0 GLOWLANG_BIN=./bin/glowlang_asan ./run_all_tests.sh

# ThreadSanitizer (kernel >= 6.6 needs ASLR disabled)
GLOWLANG_BIN=./bin/glowlang_tsan setarch $(uname -m) -R ./run_all_tests.sh
```

## Fuzzing

```bash
make fuzz-parser    # 1000 random-string iterations
```

Additional fuzzers in `tests/c_harness/`:
- `fuzz_bytecode.c` — Random bytecode sequences (500 iterations)
- `fuzz_runtime.c` — Random valid programs (200 iterations)
- `fuzz_gc.c` — GC torture test (100 iterations)
- `fuzz_string.c` — String engine stress (300 iterations)
- `fuzz_module.c` — Module loader edge cases (100 iterations)
