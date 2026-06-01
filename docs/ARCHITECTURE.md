# GlowLang Architecture

The following diagram illustrates the complete architectural pipeline of GlowLang, from source code ingestion down to native machine code execution, garbage collection, and ecosystem tooling.

```mermaid
graph TD
    %% Styling
    classDef frontend fill:#1e1e2e,stroke:#3b82f6,stroke-width:2px,color:#fff
    classDef opt fill:#1e1e2e,stroke:#f59e0b,stroke-width:2px,color:#fff
    classDef vm fill:#1e1e2e,stroke:#10b981,stroke-width:2px,color:#fff
    classDef jit fill:#1e1e2e,stroke:#ef4444,stroke-width:2px,color:#fff
    classDef mem fill:#1e1e2e,stroke:#8b5cf6,stroke-width:2px,color:#fff
    classDef tool fill:#1e1e2e,stroke:#6366f1,stroke-width:2px,color:#fff

    subgraph Frontend [1. Frontend & Ingestion]
        A[Source Code / REPL] -->|flex| B(Lexer)
        B -->|Tokens| C(Parser - bison)
        C -->|Abstract Syntax Tree| D(Compiler)
    end
    class Frontend,A,B,C,D frontend

    subgraph Optimizer [2. Ahead-of-Time Optimization]
        D -->|Raw Bytecode| E[Bytecode Optimizer]
        E -->|Pass 1: Block Boundaries| E
        E -->|Pass 2: Constant Folding| E
        E -->|Pass 3: Scalar Replacement| E
        E -->|Pass 4: SIMD Auto-Vectorization| E
    end
    class Optimizer,E opt

    subgraph Runtime Execution [3. Runtime & VM]
        E -->|Optimized Bytecode| F(Virtual Machine)
        F -->|Direct-Threaded Dispatch| F
        F -->|Record Exec Count| G{Is Hot?}
    end
    class Runtime Execution,F,G vm

    subgraph JIT Compiler [4. Adaptive JIT Compilation]
        G -->|Yes | H(Type Specialization)
        H -->|Compute Signature| I[JIT Emitter]
        I -->|x86-64 Assembler| J[Native Machine Code]
        I -->|ARM64 / WASM| K[Cross-Platform Backends]
        J -->|PIC Cache Hit| J
        J -->|Guard Failure| L(Deoptimization)
        L -->|Restore State| F
    end
    class JIT Compiler,H,I,J,K,L jit

    subgraph Memory & Object Model [5. Memory Management]
        F -.-> M[(Generational GC)]
        J -.-> M
        M -.-> N[Hidden Classes / Shapes]
        M -.-> O[PackedArrays / SIMD Data]
        M -.-> P[Arena Allocator]
    end
    class Memory & Object Model,M,N,O,P mem

    subgraph Ecosystem & Tooling [6. Tools & Standard Library]
        Q[gdu Package Manager] -.-> A
        R[Language Server Protocol] -.-> A
        S[JIT-Aware Debugger & Profiler] -.-> F
        T[Native Modules: math, crypto, net, os] -.-> F
    end
    class Ecosystem & Tooling,Q,R,S,T tool
```

## Subsystem Breakdown

1.  **Frontend**: GlowLang uses `flex` and `bison` to parse its Natural Language-like syntax into an AST.
2.  **Optimizer**: Before execution, bytecode is heavily optimized. Most notably, loops iterating over `PackedArrays` are collapsed into single `OP_SIMD_ADD` instructions.
3.  **Virtual Machine**: A highly optimized, direct-threaded VM handles initial execution and profiling.
4.  **JIT Compiler**: When a function gets hot, the JIT records the types of its arguments (Type Specialization) and emits native machine code. Call sites use Polymorphic Inline Caches (PICs) to jump directly from one JIT function to another without hitting the C-bridge.
5.  **Memory**: Generational mark-and-sweep GC is used for objects, while an Arena allocator handles fast AST memory. Object shapes (Hidden Classes) enable fast property access.
6.  **Ecosystem**: `gdu` manages dependencies, while native C-bindings expose raw performance to the standard library.
