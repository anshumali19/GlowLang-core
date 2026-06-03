# NEXT STEPS - Road map for Phase 4

With the performance foundation in place, the next phase will focus on expanding the JIT capabilities and enhancing the concurrency model.

### Phase 4.1: Direct Call Linking
- **Goal**: Patch JIT call sites to jump directly to target machine code addresses.
- **Impact**: Eliminate the C-helper bridge overhead, targeting <0.01s for Fib(30).

### Phase 4.2: SIMD Tensor Operations
- **Goal**: Implement SIMD-accelerated `tensor.add`, `tensor.sub`, and `tensor.dot`.
- **Optimization**: Use AVX2/FMA for fused multiply-add operations in neural network layers.

### Phase 4.3: Concurrent Garbage Collection
- **Goal**: Implement a mark-and-sweep GC that runs on a background thread.
- **Impact**: Reduce "stop-the-world" pauses in real-time applications and GUI loops.

### Phase 4.4: ARM64 Backend Expansion
- **Goal**: Full implementation of the AArch64 JIT for Apple Silicon and ARM servers.
- **Status**: Structural foundation established; need instruction mapping for `OP_CALL` and `OP_ADD`.

### Phase 4.5: Type-Specialized String Ops
- **Goal**: JIT-emit specialized code for Rope flattening and substring access.
- **Impact**: Zero-cost string manipulation for high-throughput text processing.
