# GlowLang Architecture

## Overview

GlowLang is implemented in C. A `.glow` file is lexed, parsed to an AST, compiled to register-based
bytecode, optimized, and run by a virtual machine that compiles hot functions and loops to x86-64
machine code. Memory is managed by a non-moving mark-and-sweep garbage collector.

This document describes what the code does today. Measured performance is in
[benchmarks/RESULTS.md](benchmarks/RESULTS.md).

---

## Execution Pipeline

```
Source (.glow)
    ↓  Flex lexer            (src/glowlang.l)
  Tokens
    ↓  Bison parser          (src/glowlang.y)
  AST
    ↓  Compiler              (src/compiler.c)
  Register bytecode
    ↓  Bytecode optimizer    (src/opt.c)
    ↓  Bytecode Optimizer    (src/opt.c)
  Optimized bytecode
    ↓
  Interpreter  ⇄  JIT        (src/vm.c ⇄ src/jit.c)
    ↓
  Mark-and-sweep GC          (src/gc.c)
```

---

## Core Components (`src/`)

| File | Role |
|------|------|
| `glowlang.l` | Flex lexer; also the identifier/string intern table (resizable). |
| `glowlang.y` | Bison grammar and the `main` entry point (subcommands `run`, `repl`, `check`, `trace`, `debug`, `package`). |
| `glowlang.h` | `Value` representation, object layouts, opcodes. |
| `compiler.c` | AST → register bytecode. Python-like scoping: a name assigned anywhere in a function is local to the whole function unless it is a declared global. |
| `opt.c` | Bytecode passes: constant folding and type inference, scalar replacement, one loop pattern rewritten to `OP_SIMD_ADD`, tail-call and self-call marking. |
| `vm.c` | Interpreter loop, calls and frames, exceptions, threads and the GIL, imports. |
| `jit.c` | x86-64 template JIT, Microsoft x64 and System V ABIs. |
| `gc.c` | Garbage collector. |
| `stdlib.c` | Native modules (`http`, `sqlite`, `tensor`, `fs`, `json`, `os`, `crypto`, `threads`, …). |
| `regex.c` | Backtracking regular-expression engine used on every OS. |
| `bigint.c` | Arbitrary-precision integers. |
| `lsp.c` | Language Server Protocol (JSON-RPC 2.0). |
| `arena.c` | Bump allocator for AST nodes. (`glow_alloc_guarded`, a guard-page allocator, is defined here but nothing uses it yet.) |
| `win_pthread.h` | pthread mutex/condition-variable/thread shim over Win32 (SRW locks, condition variables). |
| `cuda/tensor_ops.cu` | Optional CUDA kernels, built only when `nvcc` is found. |
| `glowlang.l` | Flex lexer — tokenizes source. |
| `glowlang.y` | Bison grammar (447 lines) — builds AST nodes. |
| `glowlang.h` | Core type definitions, the `Value` representation, and opcode definitions. |
| `compiler.c` | Lowers AST into register-based bytecode; handles symbol table and code generation. |
| `vm.c` | The virtual machine execution loop, inline caching, and opcode dispatch. |
| `jit.c` | Native x86-64 JIT compiler (dual Microsoft x64 and System V AMD64 ABI backends). |
| `opt.c` | Bytecode optimization passes (constant folding, dead instruction elimination). |
| `gc.c` | Generational garbage collector (2 MB young generation nursery + 8 MB tenured heap). |
| `stdlib.c` | Native builtins (HTTP, SQLite, AVX2 SIMD tensors, filesystem, JSON, crypto, etc.). |
| `lsp.c` | Language Server Protocol (JSON-RPC 2.0) implementation. |
| `arena.c` | Bump-pointer arena allocator for AST nodes and hardware MMU guard page allocations. |
| `bigint.c` | Arbitrary-precision integer arithmetic routines. |
| `win_pthread.h`| Windows POSIX-thread shim enabling native multi-core concurrency without GIL. |
| `cuda/tensor_ops.cu` | Optional CUDA kernels (see *Hardware Acceleration*). |

### Value Representation

Values are NaN-boxed into a `uint64_t`. Doubles are stored as themselves; other types carry a tag in
the high bits. Integers are 48-bit inline values. Arithmetic that leaves that range promotes to an
exact double (up to 2^53) and then to a heap big integer, so integers never wrap. A consequence:
`type()` reports `float` for integers between 2^47 and 2^53, although they print and compare as exact
integers.

### Virtual Machine

A register VM. Frames for GlowLang-to-GlowLang calls are pushed on an explicit frame array, not the C
stack. Each `run_bytecode` activation (for example a native callback) uses its own slice of that
array. Dispatch uses computed `goto` when built with GCC or Clang, and a `switch` with MSVC (the
default Windows build). Recursion is limited to 65,500 nested calls and also stops before the
thread's machine stack runs out; both raise a catchable "Maximum recursion depth exceeded" error.

### JIT

- A function is compiled by the **baseline JIT** after it has run 5 times and by the **optimizing
  JIT** after 50. Functions called 10+ times with stable argument types also get a type-specialized
  version.
- **Hot loops** are compiled mid-execution (on-stack replacement) after 1,000 back-edge iterations.
- Compiled code reads and writes the same VM register file as the interpreter. Anything it cannot
  handle exactly (an unsupported operation, a failed type guard, a raised exception) leaves compiled
  code at that instruction and the interpreter continues in the same frame.
- Integer arithmetic checks for overflow and falls back to the numeric tower.
- x86-64 only; on other CPUs everything runs in the interpreter.

### Garbage Collector

- Small objects are bump-allocated in 1 MB blocks; large ones are allocated individually. Objects
  never move.
- Roots: globals, every thread's VM register stack, pending exceptions and spawn arguments
  (precise), plus a conservative scan of every thread's native stack and saved registers, so values
  held only in C locals stay alive.
- A full collection runs when allocation exceeds max(32 MB, 2 × the live size after the last one).
- A generational mode exists behind `GLOW_GC_GENERATIONAL=1`. It is experimental: a minor collection
  still walks the whole object list.
- Collection is stop-the-world; with the GIL, only one thread mutates the heap at a time.
### Tiered JIT & Monomorphic Inline Caching

`jit.c` promotes frequently executed code through tiers:

1. **Interpreter with Monomorphic Inline Caching** — all code starts in `vm.c`. Generic bytecode
   instructions (`OP_ADD`, `OP_SUB`, `OP_MUL`, `OP_LT`, etc.) dynamically rewrite their opcodes in-place
   to specialized integer/float variants (`OP_ADD_INT`, `OP_SUB_INT`, etc.) upon observing stable operand
   types, eliminating type inspection dispatch overhead.
2. **Baseline JIT** — compiled after hot loop / invocation count thresholds.
3. **Optimizing Native JIT** — emits native x86-64 machine instructions into executable memory pages.
   Supports both **System V AMD64 ABI** (Linux / macOS: args in RDI, RSI, RDX, RCX, R8, R9) and
   **Microsoft x64 ABI** (Windows: args in RCX, RDX, R8, R9, with 32-byte shadow store and 16-byte stack
   alignment). Guards can deoptimize back to the interpreter when type assertions fail.

### Generational Garbage Collector

`gc.c` implements a generational collector:
- **Generation 0 (Nursery, 2 MB threshold)**: New object allocations land in Gen 0. Minor collections
  scavenge active roots and promote surviving objects directly into Gen 1 without traversing tenured memory.
- **Generation 1 (Tenured Heap, 8 MB threshold)**: Long-lived objects are tracked across generational
  write barriers and swept via mark-and-sweep.
- The arena allocator (`arena.c`) handles bump-pointer AST and compiler data structures that have
  phase-bounded lifetimes.

### Multi-Core Concurrency & Memory Protection

- **Abolished GIL Concurrency**: The `spawn` keyword provisions true native OS threads (`CreateThread` on
  Windows, `pthread_create` on POSIX). Each thread operates on an isolated VM execution context with
  thread-local register stacks, achieving full CPU core parallelization without Global Interpreter Lock contention.
- **Hardware MMU Guard Pages**: High-assurance memory buffers (`glow_alloc_guarded` in `arena.c` and
  `lib/kavach.glow`) allocate memory flanked by CPU MMU guard pages (`PAGE_NOACCESS` / `PROT_NONE`),
  immediately trapping buffer overruns at the hardware fault level.

### Concurrency

`spawn f(args)` starts an OS thread. All threads share globals and the heap, so a **global
interpreter lock** (a FIFO ticket lock) lets one thread run GlowLang code at a time. It is released
during `os.sleep`, `os.system` and HTTP/TLS socket calls, and handed to a waiting thread at loop
back-edges and calls. `threads.join_all()` waits for spawned threads, and so does program exit (except
`whenever` monitors, which are daemon threads). There is no parallel execution of GlowLang code.

### Strings

Strings are flat, length-prefixed byte arrays. `a + b` builds a flat string when the result is at
most 256 bytes and a rope node otherwise, so repeated appends stay linear; a rope is flattened once,
the first time its bytes are needed. `len` is constant-time.

### SIMD

Tensor add/sub/mul/dot/matmul have AVX2 (+FMA for dot and matmul) kernels compiled with per-function
target attributes and chosen at run time by CPUID, with scalar fallbacks. `GLOW_NO_SIMD=1` forces the
scalar code. The binary itself does not require AVX2.

---

## Standard Library

- **Native modules** in `src/stdlib.c`.
- **GlowLang modules** in `lib/` (`web.glow`, `db.glow`, `orm.glow`, `ai.glow`, `glowtorch*`,
  `dataframe.glow`, `testing.glow`, …), loaded with `use "lib/x.glow"` or `import "lib/x.glow"`.
  A module's top-level variables are globals, visible to its functions and to the importer.

---

## Build System

| Platform | Command | Notes |
|---|---|---|
| Windows | `build.bat` → `scripts\build-windows.ps1` | MSVC by default (`-Compiler clang` for clang-cl), static OpenSSL/SQLite from vcpkg, writes a `.pdb` for symbolized crash reports, signs with `scripts\sign-windows.ps1`. |
| Linux | `make` | GCC, Flex, Bison, libsqlite3, libssl, libX11. |
| Any | `CMakeLists.txt` | Used by CI on Linux, macOS and Windows. |

Other make targets: `make test`, `make test-sanitized` (ASan + TSan), `make fuzz-all`, `make tools`,
`make wasm` (Emscripten; not verified recently).

---

## Testing

- `tests/*.glow` (129 programs). `scripts/run-tests.ps1` (Windows) and `scripts/run-tests.sh`
  (Linux/macOS/Git Bash) run each with a timeout and check the exit code
  (`tests/expected/exit_codes.txt`), compare stdout with `tests/expected/<name>.out` (`{{ANY}}`
  matches variable text such as timings), and check required stderr lines (`<name>.err`).
  `tests/expected/modes.txt` marks thread tests whose lines may interleave.
- Useful modes: `GLOW_NOJITEXEC=1` (interpreter only), `GLOWLANG_GC_STRESS=1` (collect every 64 KB),
  `GLOW_FORCEFULL=1` (collect on every allocation; slow), `GLOW_NO_SIMD=1`.
- `tests/c_harness/` — C fuzz harnesses.
- `benchmarks/run-benchmarks.ps1` regenerates `benchmarks/RESULTS.md`.
- `tests/*.glow` — language-level test cases run by `run_all_tests.sh` and `run_all_tests.bat`. The runner checks each
  program's **exit code** against an expected value (most expect `0`; a few expect specific non-zero
  codes), so it verifies that programs run to completion. All 117 cases pass with 100% success.
- `tests/c_harness/` — C fuzz harnesses (parser, bytecode, runtime, GC, string, module).
- `tests/integration/`, `tests/stability/` — larger integration and stability scenarios.

---

## Repository Layout

```
GlowLang/
├── src/            # C core: lexer, parser, compiler, VM, JIT, GC, stdlib, regex, LSP
├── lib/            # standard library written in GlowLang
├── tests/          # .glow tests, tests/expected/, C fuzz harnesses
├── benchmarks/     # benchmark programs, runner, RESULTS.md
├── scripts/        # Windows build/sign scripts, test runners
├── examples/       # example programs
├── editors/        # editor integrations
├── tools/          # gdu, debugger, profiler, inspector
├── docs/           # guides and language reference
├── Makefile
└── CMakeLists.txt
```
