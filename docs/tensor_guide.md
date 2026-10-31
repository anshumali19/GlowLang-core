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

c = tensor.add(a, b)    # Element-wise add (AVX2 when the CPU has it)
d = tensor.sub(a, b)    # Element-wise subtract
e = tensor.mul(a, b)    # Element-wise multiply
```

The `+`, `-` and `*` operators do not work on tensors; use these functions.

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

- Element-wise add, subtract, multiply, dot and matmul use AVX2 + FMA when the CPU has them (checked at run time), and plain scalar code otherwise (also on ARM64, and with GLOW_NO_SIMD=1)
- The work happens inside the native `tensor.*` functions; a loop over elements in GlowLang code is much slower than one call such as `tensor.add`
- Maximum tensor size: 10,000,000 elements (`tensor.create`, `zeros` and `ones` return `none` for larger sizes)
