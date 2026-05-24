# GlowLang 2.0 Benchmarks

**Date:** May 24, 2026
**Environment:** Linux (Ubuntu 22.04), AMD Ryzen/Intel Core (Virtual Environment)

## Comparison with Python 3.10

| Benchmark | GlowLang 2.0 | Python 3.10 | Winner |
|-----------|--------------|-------------|--------|
| **Fibonacci (30)** | 0.189s | 0.120s | Python (1.5x) |
| **Simple Loop (10M)** | 0.495s | 0.682s | GlowLang (1.3x) |
| **Object System (1M)** | 0.149s | 0.082s | Python (1.8x) |

### Analysis
- **Loops**: GlowLang's direct-threaded VM and Nan-boxing make simple arithmetic loops very efficient, outperforming Python's standard interpreter.
- **Recursion**: Python is currently faster at function call overhead, likely due to its mature stack frame management.
- **Object System**: With the introduction of **Hidden Classes (Shapes)** and **Inline Caching (IC)**, GlowLang's property access is remarkably competitive for a new implementation.

### Key Optimizations
1. **Direct Threading**: Reduces dispatch overhead.
2. **Nan-Boxing**: Efficient value representation (64-bit).
3. **Hidden Classes**: Fast property mapping.
4. **Inline Caching**: Bypass lookups for stable shapes.
5. **Experimental JIT**: Compiled x86-64 code for hot paths.
