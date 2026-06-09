# GlowLang

GlowLang is an adaptive, high-performance systems programming language designed to be as readable as natural language while executing at native speeds.

## Features

- **Natural Language Syntax**: Read and write code using keywords like `blueprint`, `action`, `every`, `is`, and `is not`.
- **Adaptive JIT Compiler**: Automatically identifies hot paths and vectorizable loops, compiling them to optimized machine code (x86-64, ARM64) on the fly.
- **SIMD Auto-Vectorization**: Native support for `PackedArray` operations, allowing matrix and data processing to execute at theoretical hardware peaks.
- **Cross-Platform**: Supports Linux, macOS (Apple Silicon), Windows, and WebAssembly (WASM).
- **Zero-Cost Abstractions**: Traits, Classes, and Exception Handling incur zero performance overhead.
- **Built-In Async Runtime**: Scalable, thread-safe asynchronous concurrency (`spawn`, `can`) leveraging a Global Interpreter Lock (GIL) and lock-free thread pooling.

## Quick Start

### Building from Source
Ensure you have `gcc`, `flex`, and `bison` installed, then run:

```bash
make
```

### Running the REPL
Start the interactive interpreter by running the binary without arguments:

```bash
./bin/glowlang
```

### Running a Script
```bash
./bin/glowlang examples/hello.glow
```

## The Ecosystem

GlowLang ships with a rich native ecosystem designed for backend services, AI/ML pipelines, and scripting:

- **`gdu (Glow Dependency Utility)` Package Manager**: Install dependencies via `gdu (Glow Dependency Utility) install <pkg>`.
- **Native Modules**: 
  - `math`: Accelerated math functions (`sin`, `cos`, `sqrt`).
  - `crypto`: High-speed OpenSSL hashing (`sha256`).
  - `sqlite`, `net`, `http`, `os`.

Refer to `docs/TUTORIAL.md` for a comprehensive guide to the language.
