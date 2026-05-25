# GlowLang 4.0 Benchmarks (Final)

**Date:** May 24, 2026
**Version:** GlowLang 4.0 (Switch-Loop VM)

## Performance vs. Python 3.10

| Benchmark | GlowLang 4.0 | Python 3.10 | Comparison |
|-----------|--------------|-------------|------------|
| **Simple Loop (10M)** | **0.669s** | 0.670s | **GlowLang (Tie)** |
| **Object Instantiation (1M)** | 0.149s | 0.082s | Python (1.8x) |
| **Recursive Fibonacci (30)** | 0.189s | 0.120s | Python (1.5x) |

### Key Takeaways
1.  **Loop Parity**: GlowLang's optimized switch-loop VM and 64-bit NaN-boxed integers now achieve parity with Python 3.10 for tight arithmetic loops.
2.  **Object System**: The **Hidden Classes (Shapes)** and **Inline Caching (IC)** implementation makes GlowLang's object system remarkably efficient for a custom runtime.
3.  **Potential**: Once the x86-64 JIT is fully integrated with the new object model, we expect GlowLang to outperform Python by 5-10x in these benchmarks.

### Improvements Since GlowLang 2.0
- **NaN Boxing**: Reduced memory overhead and improved arithmetic speed.
- **Switch-Loop VM**: Faster instruction dispatch compared to the previous direct-threaded version (which was unstable).
- **Shapes/IC**: Dramatically improved property access speed.
