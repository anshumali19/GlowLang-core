# GlowLang - Roadmap & Next Steps

## Current Status
- [x] **Phase 1-3: Fast-Glow VM** (Direct Threading, NaN Boxing, Index-based locals).
- [x] **Phase 4: Module System** (`import` keyword, recursive worklist loader).
- [x] **Phase 5: Natural Library** (`natural.glow`, concise English naming).
- [x] **Phase 6: Garbage Collection** (Mark-and-Sweep).
- [x] **Phase 7: JIT Compilation** (Initial x86-64 Emitter).
- [x] **Phase 12: Natural Language Expansion** (`is`, `is in`, `every...from X to Y`).

## Immediate Roadmap (High-Performance Engineering)
- [x] **Phase 8: Production-Grade JIT (Register Allocation)**.
    - Implemented a Linear Scan Register Allocator.
    - Mapped local variables to CPU registers (`%rbx`, `%r12-r15`).
    - Successfully resolved native stack restoration edge-cases.
- [x] **Phase 10: Advanced Optimizations (Hidden Classes & Inline Caching)**.
    - Implemented a shape-based object system.
    - Added Inline Caching (IC) for property access and method calls.
    - Verified significant performance gains in object-oriented code.
- [x] **Phase 11: Traits (Modular Reuse)**.
    - Added `trait` / `end` blocks.
    - Implemented `use` keyword for class composition.
- [x] **Phase 13: `gdu (Glow Dependency Utility)` Package Manager (Production-Ready)**.
    - Implemented `Glow.lock` for project reproducibility.
    - Added DJB2-based file hash verification for security.
- [x] **Phase 14: Standard Library (Initial Modules)**.
    - Implemented `os` module (exit, time, system).
    - Implemented `json` module (parse, stringify).
    - Added a generic **Native Function** interface for easy C expansion.

## Immediate Focus (Industry Readiness)
1. **Standard Library Expansion**: Implement `net`, `http`, and `fs` (advanced file ops) modules.
2. **Arbitrary Precision Math**: Implement boxed 64-bit or BigInt support for industry-grade math.
3. **Concurrency Model**: Design and implement a lightweight threading or async/await model.
4. **VS Code Extension (LSP)**: Finalize the Language Server Protocol implementation.

**Overall Progress: 85%**
    - Implement a **Language Server Protocol (LSP)** in C.
    - Add **Autocomplete**, **Go-to-Definition**, and **Syntax Highlighting**.
    - Release the official `GlowLang-VSCode` extension.

## Ultimate Objectives
- [ ] **Phase 15: Platform Independence**
    - Implement a cross-platform portability layer (Posix/Win32).
    - Support ARM64 (AArch64) in the JIT backend.
    - Native builds for Windows, macOS, and Linux.

## Architectural Notes
- **NaN Boxing**: All values are 64-bit uint64. This is stable and has yielded ~3.5x speedup.
- **Direct Threading**: Label-based dispatch is stable and significantly faster than `switch`.
- **JIT Strategy**: Custom x86-64 backend that targets "hot" paths identified by backward-branch profiling.
