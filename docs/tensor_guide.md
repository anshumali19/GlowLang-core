# GlowLang Tensor & ML Guide

## Creating Tensors

```glow
t = tensor.create(100)     # Uninitialized float64 array
z = tensor.zeros(50)       # Zero-filled
o = tensor.ones(50)        # One-filled
```

## Element-wise Operations

Tensors support SIMD-accelerated element-wise operations:

```glow
a = tensor.ones(1000)
b = tensor.ones(1000)

c = a + b          # Element-wise add (AVX2/SSE4.2)
d = a - b          # Element-wise subtract
e = a * b          # Element-wise multiply
```

## Indexing

```glow
t = tensor.create(10)
t[0] = 3.14
t[1] = 2.71
show t[0]          # 3.14
```

## Matrix Multiplication

```glow
a = tensor.create(9)   # 3x3 matrix stored flat
b = tensor.create(9)
# Fill matrices...
result = tensor.dot(a, b)
```

## Memory Layout

Tensors are backed by `PackedArray` objects with `PACKED_FLOAT64` type. They are contiguous float64 arrays managed by the GC, with SIMD alignment for vectorized operations.

## Performance Notes

- Operations use AVX2 and SSE4.2 when available
- The `OP_SIMD_ADD`, `OP_PACKED_ADD`, `OP_PACKED_MUL` opcodes are JIT-compiled
- Maximum tensor size: 10,000,000 elements
