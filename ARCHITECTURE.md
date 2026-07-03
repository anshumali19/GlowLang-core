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
    ↓  Optimizer (SSA)       (src/opt.c, src/ssa.c)
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
| `glowlang.y` | Bison grammar — builds the AST. |
| `glowlang.h` | Core type definitions, the `Value` representation, and the opcode set. |
| `compiler.c` | Lowers the AST into register-based bytecode. |
| `vm.c` | The virtual machine and its ~120 opcodes. |
| `jit.c` | Multi-tier JIT compiler and deoptimization. |
| `opt.c`, `ssa.c` | SSA-based optimization passes over the bytecode/MIR. |
| `gc.c` | Generational garbage collector. |
| `stdlib.c` | Native builtins (HTTP, SQLite, tensors, filesystem, JSON, OS, etc.). |
| `lsp.c` | Language Server Protocol implementation. |
| `arena.c` | Bump-pointer arena allocator for AST/compilation data. |
| `ir.c`, `ast.c`, `run_bytecode_optimized.c` | Supporting IR and runtime helpers. |
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

### Tiered JIT

`jit.c` promotes frequently executed code through tiers:

1. **Interpreter** — all code starts here.
2. **Baseline JIT** — a function/loop is compiled after ~10 executions.
3. **Optimizing JIT** — recompiled after ~100 executions.

The JIT emits native machine code into executable memory. Guards can **deoptimize** back to the
interpreter (via the `DEOPT` / `TRACE_EXIT` sentinels) when a speculative assumption fails. Inline
caches accelerate property access and method dispatch.

### Garbage Collector

`gc.c` is a generational collector managing heap-allocated objects (strings, lists, dicts, objects,
closures). The arena allocator (`arena.c`) is used for compile-time data that has a bump/free
lifetime rather than being GC-managed.

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

- `tests/*.glow` — language-level test cases run by `run_all_tests.sh`. The runner checks each
  program's **exit code** against an expected value (most expect `0`; a few expect specific non-zero
  codes), so it verifies that programs run to completion, not full output correctness. All 104
  cases pass as of the last local run.
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
