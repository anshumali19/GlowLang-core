# GlowLang Architecture

## Overview

GlowLang is a programming language with natural-language syntax, implemented in C. A `.glow`
source file is lexed, parsed to an AST, compiled to register-based bytecode, optimized, and then
executed by a virtual machine that can promote hot code to native machine code via a tiered JIT.
Memory is managed by a generational garbage collector.

This document describes the components that actually exist in the repository.

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
    ↓  Bytecode Optimizer    (src/opt.c)
  Optimized bytecode
    ↓
  Register VM  ⇄  Tiered JIT (src/vm.c ⇄ src/jit.c)
    ↓
  Generational GC            (src/gc.c)
```

---

## Core Components (`src/`)

| File | Role |
|------|------|
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

Values are **NaN-boxed** into a single `uint64_t` (`typedef uint64_t Value`). The type tag lives in
the high bits and is recovered via `GET_TYPE`. Value types are `INT`, `FLOAT`, `BOOL`, `STRING`,
`LIST`, `DICT`, `OBJECT`, and `NONE`, plus internal `DEOPT` / `TRACE_EXIT` sentinels used by the JIT.
Floats are stored directly; other types pack a tag and payload (immediate value or heap pointer).

### Virtual Machine

`vm.c` implements a **register-based** VM. On supported compilers the dispatch loop uses
**direct-threaded / computed-goto** dispatch (`goto *dt[pc->op]`), with a `switch`-based fallback for
portability. The VM maintains localized frame pointers for call frames and enforces instruction and
recursion limits as safety guards.

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

---

## Standard Library

The standard library has two layers:

- **Native builtins** in `src/stdlib.c` — modules exposed to Glow programs such as `http`, `sqlite`,
  `tensor`, `fs`, `json`, `os`, and math/string helpers. Tensor operations use SIMD (AVX2/SSE4.2).
- **Glow-language modules** in `lib/` — libraries written in GlowLang itself, e.g. `web.glow`,
  `orm.glow`, `ai.glow`, the `glowtorch*` autograd/NN modules, `tensor_utils.glow`, `dataframe.glow`,
  and testing utilities.

---

## Hardware Acceleration (Optional)

Tensor math runs on the CPU by default using SIMD. A CUDA path (`src/cuda/tensor_ops.cu`) is compiled
**only when `nvcc` is available** — the Makefile detects it and defines `GLOW_USE_CUDA`, linking
`-lcudart`/`-lcublas`. Without an NVIDIA toolchain the `.to_device("cuda")` abstraction falls back to
the CPU implementation. Planned GPU work is tracked in `nextpa.md`.

---

## Tooling

| Area | Location |
|------|----------|
| Package manager (`gdu`) | `tools/gdu/` |
| Debugger | `tools/debugger/` |
| Profiler | `tools/profiler/` |
| Heap/bytecode inspector | `tools/inspector/` |
| AOT experiments | `tools/aot/` |
| Stack visualizer | `tools/stack_visualizer/` |
| Web playground | `tools/playground/`, `web/playground/` |
| Editor integrations | `editors/` (VS Code, Neovim, Sublime, Helix, JetBrains) |

The VS Code extension (`editors/glowlang-vscode/`) speaks to the LSP server built from `src/lsp.c`.

---

## Build System

The primary build is the `Makefile` (Flex + Bison + GCC). It requires `libsqlite3`, `libX11`,
`libssl`/`libcrypto`, and an x86-64 CPU with AVX2/SSE4.2. Key targets:

- `make all` — build the `glowlang` interpreter and the `gdu` package manager.
- `make verify` — build and run the test suite (`run_all_tests.sh`).
- `make test-sanitized` — build and run tests under AddressSanitizer and ThreadSanitizer.
- `make fuzz-all` — run the parser/bytecode/runtime/GC/string/module fuzz harnesses.
- `make tools` — build the standalone debugger, profiler, and inspector.
- `make wasm` — build a WebAssembly target via Emscripten.

A `CMakeLists.txt` is also provided for cross-platform CI (Linux, macOS, Windows).

---

## Testing

- `tests/*.glow` — language-level test cases run by `run_all_tests.sh` and `run_all_tests.bat`. The runner checks each
  program's **exit code** against an expected value (most expect `0`; a few expect specific non-zero
  codes), so it verifies that programs run to completion. All 117 cases pass with 100% success.
- `tests/c_harness/` — C fuzz harnesses (parser, bytecode, runtime, GC, string, module).
- `tests/integration/`, `tests/stability/` — larger integration and stability scenarios.

---

## Repository Layout

```
GlowLang/
├── src/            # C core: lexer, parser, compiler, VM, JIT, GC, stdlib, LSP
│   └── cuda/       # optional CUDA kernels
├── lib/            # standard library written in GlowLang
├── tests/          # .glow test cases + C fuzz harnesses
├── benchmarks/     # cross-language benchmark sources + report
├── examples/       # example programs
├── editors/        # editor integrations (VS Code, Neovim, Sublime, Helix, JetBrains)
├── tools/          # gdu, debugger, profiler, inspector, and more
├── deploy/         # Docker / deployment configs
├── docs/           # guides and language reference
├── project/        # sample project layout
├── web/            # web playground
├── Makefile        # primary build
└── CMakeLists.txt  # cross-platform build for CI
```
