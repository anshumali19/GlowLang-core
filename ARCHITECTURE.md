# GlowLang Architecture

## Overview

GlowLang is an Indian-origin systems programming language project focused on performance, innovation, tooling, and long-term ecosystem development.

The repository contains:

* the Glow language compiler and parser infrastructure,
* benchmarking systems for cross-language performance evaluation,
* editor tooling and Language Server Protocol (LSP) integrations,
* runtime and utility tooling,
* and experimental research infrastructure.

The project emphasizes:

* systems-level performance,
* language tooling,
* cross-language benchmarking,
* and enterprise-ready ecosystem architecture.

---

# Repository Architecture

## 1. Parser and Compiler Infrastructure (`archive/`)

The `archive/` directory contains multiple historical parser implementations and compiler experiments for GlowLang.

### Components

* Parser generations (`v3` → `v6`)
* AST construction logic
* tokenization and grammar systems
* parser-generator outputs (Bison/Yacc style)
* syntax evolution experiments

### Core Structure

The parser architecture revolves around the `GlowValue` AST node representation containing:

* type metadata
* numeric values
* string values

This layered parser evolution demonstrates iterative compiler development and grammar refinement over time.

### Design Goals

* incremental compiler evolution
* experimental parser research
* syntax validation
* runtime integration preparation
* future optimization support

---

## 2. Benchmarking Infrastructure (`benchmarks/`)

GlowLang includes a comprehensive multi-language benchmark suite designed for performance validation and systems-level comparison.

### Supported Languages

| Language             | Implementation                                 |
| -------------------- | ---------------------------------------------- |
| C                    | `dijkstra_c.c`                                 |
| C++                  | `bench_dijkstra.cpp`                           |
| C#                   | `bench_cs.cs`, `BenchLoop.cs`, `DsaNQueens.cs` |
| Java                 | `BenchLoop.java`, `DijkstraBench.java`         |
| Go                   | `bench_dijkstra.go`                            |
| JavaScript / Node.js | `bench_dijkstra.js`                            |
| Python               | `bench_dijkstra.py`                            |

### Implemented Algorithms

The benchmark suite currently includes:

* Dijkstra shortest path
* N-Queens solver
* Fibonacci recursion
* Ackermann recursion
* Pi computation
* loop and iteration benchmarks

### Benchmark Philosophy

The repository follows a strict cross-language parity approach:

* identical algorithms,
* equivalent implementations,
* fair runtime comparisons,
* and reproducible benchmarking.

### Automation Infrastructure

Benchmark orchestration is handled through:

* `cross/compile_all.py`
* `scratch/run_comprehensive_bench.py`

This enables automated compilation and execution across multiple language runtimes.

---

## 3. VS Code Language Server Extension (`editors/vscode/`)

GlowLang includes a modern Language Server Protocol (LSP) implementation for Visual Studio Code.

### Architecture

The editor tooling uses:

* `vscode-languageserver-protocol`
* `vscode-languageclient`
* `vscode-jsonrpc`

### Features

The extension architecture is designed to support:

* syntax highlighting
* diagnostics
* autocomplete
* hover information
* semantic analysis
* editor integration
* future refactoring support

### Supporting Dependencies

Additional infrastructure includes:

* semantic versioning support
* minimatch-based file handling
* JSON-RPC communication
* parser integration

---

## 4. Auxiliary Tooling

The repository also contains supporting infrastructure and experimental tooling.

### Scripts (`scripts/`)

Utility scripts for:

* LSP testing
* validation
* automation
* compiler verification

### Versioning Tools (`tools/gdu/` - Glow Dependency Utility)

Includes:

* SemVer parsing utilities
* version management tooling
* release preparation infrastructure

### Runtime Experiments

Experimental systems-level testing includes:

* GC layout validation
* object offset testing
* runtime memory experiments

---

# Architectural Principles

## Polyglot Benchmarking

GlowLang maintains identical benchmark implementations across multiple programming languages to ensure fair performance analysis and runtime comparison.

## Incremental Compiler Evolution

The repository preserves historical parser generations to support long-term compiler experimentation and grammar research.

## Modern Tooling Ecosystem

The project adopts Language Server Protocol standards for professional-grade editor integration and tooling interoperability.

## Cross-Language Validation

Shared benchmark implementations across ecosystems provide validation consistency and performance reproducibility.

## Systems-Level Design

GlowLang prioritizes:

* runtime performance,
* low-level control,
* compiler experimentation,
* and scalable tooling infrastructure.

---

# Long-Term Vision

GlowLang aims to evolve into:

* a high-performance systems language,
* an enterprise-ready development ecosystem,
* a research-oriented compiler platform,
* and a globally collaborative programming language initiative.

The architecture is intentionally modular to support:

* compiler expansion,
* runtime evolution,
* editor ecosystem growth,
* package management,
* and future distributed tooling infrastructure.
