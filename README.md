# GlowLang 2.0

A high-performance, memory-safe, and philosophy-driven language with a Flex/Bison parser, bytecode compiler, iterative direct-threaded VM, and an experimental x86-64 JIT compiler — written in C.

## Quick Start

```bash
# Build (requires flex, bison, gcc)
make

# Run
./bin/glowlang examples/fib.glow

# Manage dependencies
./bin/gdu add https://url.to/module.glow
```

## Directory Structure
- **`src/`**: Core language implementation (C, Flex, Bison).
- **`tools/gdu/`**: Glow Dependency Utility (Package Manager).
- **`bin/`**: Compiled executables.
- **`examples/`**: Example GlowLang programs.
- **`tests/`**: Language test suite.
- **`benchmarks/`**: Performance benchmarks and comparison scripts.
- **`docs/`**: Documentation and architectural plans.
- **`csharp/`**: C# benchmark implementations.
- **`archive/`**: Older versions and loose development files.

## Language Philosophy
GlowLang is built on the principle of **"Natural Language First."** It prioritizes human-readable syntax and concise naming conventions (e.g., `keep` instead of `filter`, `dist` instead of `abs`, `root` instead of `sqrt`).

## Core Features

### 🚀 High Performance
- **NaN Boxing**: All values are unboxed 64-bit doubles or tagged integers/pointers, minimizing memory overhead and branches.
- **Direct Threading**: Optimized VM dispatch using label pointers for maximum execution speed.
- **Local Indexing**: Variables in functions are indexed at compile-time for $O(1)$ access.
- **JIT Compiler**: Experimental x86-64 backend that targets "hot" loops for native machine code execution (~15x speedup).

### 🛡️ Reliability & Safety
- **Iterative VM**: Custom call-stack management (4M+ depth) avoids C recursion, making the language immune to standard stack overflows.
- **Garbage Collection**: Robust Mark-and-Sweep GC manages strings, lists, dicts, and closures automatically.
- **Exception Handling**: Built-in `try` / `catch` / `throw` for graceful error recovery.

### 🧩 Modern Syntax
- **Natural Operators**: Intuitive English aliases like `is`, `is not`, `is in`, and `is not in`.
- **Range Loops**: Modern `every i in from 1 to 10:` syntax (inclusive).
- **Modules**: First-class `import` system for multi-file project management.
- **Closures**: Full lexical scope capturing with `lambda` support.
- **Functional Tools**: Built-in `apply` (map), `keep` (filter), and `avg`.
- **Comprehensions**: Python-style list comprehensions for concise data manipulation.
- **F-Strings**: Modern string interpolation with the `f"..."` syntax.

## Performance Benchmarks (May 2026)

| Benchmark | Interpreted VM | JIT Native | Python 3.12 |
|-----------|----------------|------------|-------------|
| **10M Loop** | 0.282s | **0.016s** | 0.728s |
| **Fib(30)** | 0.280s | - | 0.119s |
| **Ack(3, 8)** | 0.156s | - | 0.145s |

*GlowLang JIT achieves native speeds on tight loops, outperforming Python 3 by over **45x**.*

## Project Roadmap

- [x] **Phase 1-3**: Fast-Glow VM (Direct Threading, NaN Boxing, Index-based locals).
- [x] **Phase 4**: Module System (`import` keyword, recursive worklist loader).
- [x] **Phase 5**: Natural Library (`natural.glow`, concise English naming).
- [x] **Phase 6**: Garbage Collection (Mark-and-Sweep).
- [x] **Phase 7-8**: Production JIT Compiler (x86-64 Emitter, Register Allocation).
- [x] **Phase 9**: Formal Object System (Classes, Instances, Traits).
- [x] **Phase 10**: Advanced Optimizations (Hidden Classes/Shapes, Inline Caching).
- [x] **Phase 12**: Natural Language Syntax Expansion.
- [x] **Phase 13**: `gdu` Package Manager (Initial implementation, Lockfiles, Hash Verification).
- [ ] **Upcoming**: VS Code Extension (LSP), Standard Library Expansion.

## Core Features
- **Hidden Classes (Shapes)**: Optimized object property storage.
- **Inline Caching (IC)**: Lightning-fast property access and method calls.
- **Traits**: Composable units of behavior using the `use` keyword.
- **Nan-Boxing**: 64-bit value representation for maximum performance.
- **Direct Threading**: Fast instruction dispatch.
- **`gdu` CLI**: Modern package manager with `Glow.lock` support.

```text
┌─────────────┐
│ Source Code  │  (.glow files)
└─────────────┘
    │  lex/parse (Flex/Bison)
    ▼
┌─────────────┐
│ AST          │  (40+ node types)
└─────────────┘
    │  compile (Constant folding, Local indexing)
    ▼
┌─────────────┐
│ Bytecode     │  (Optimized stream)
└─────────────┘
    │  profiling / dispatch
    ▼
┌─────────────┐      ┌─────────────┐
│ Stack VM    │ ◄─── │ x86-64 JIT  │
└─────────────┘      └─────────────┘
    (Iterative)        (Native Code)
```

## License
Educational / experimental project.
