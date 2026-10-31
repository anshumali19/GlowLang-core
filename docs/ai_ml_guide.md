# GlowLang AI/ML Guide

A tutorial for building AI and machine learning applications in GlowLang — covering tensor operations, SIMD-accelerated computation, neural networks, matrix math, and auto-differentiation.

---

## Why GlowLang for AI/ML?

GlowLang provides native tensor operations backed by SIMD hardware acceleration:

- **AVX2/SSE** on x86-64 for 256-bit parallel computation
- **NEON** on ARM64 for mobile/edge AI
- **Zero-copy tensors** allocated outside the GC for predictable performance
- **Auto-vectorization** by the JIT — numeric loops are compiled to SIMD instructions

---

## 1. Tensor Basics

### Creating Tensors
```glow
# Zero-filled tensor
let zeros = tensor.zeros(10)
show "Zeros:", zeros

# Uninitialized tensor (for manual filling)
let t = tensor.create(5)

# Fill with values
let i = 0
while i < 5:
    t[i] = i * 1.0
    i = i + 1
end
show "Tensor:", t    # [0.0, 1.0, 2.0, 3.0, 4.0]
```

### Tensor Indexing
```glow
let a = tensor.zeros(5)
a[0] = 1.0
a[1] = 2.0
a[2] = 3.0

show "Element 0:", a[0]   # 1.0
show "Element 2:", a[2]   # 3.0
```

---

## 2. Element-wise Operations

### Addition
```glow
let a = tensor.zeros(5)
let b = tensor.zeros(5)

let i = 0
while i < 5:
    a[i] = i * 1.0       # [0, 1, 2, 3, 4]
    b[i] = i * 2.0       # [0, 2, 4, 6, 8]
    i = i + 1
end

let c = tensor.add(a, b)
show "A + B:", c           # [0, 3, 6, 9, 12]
```

### Multiplication
```glow
let d = tensor.mul(a, b)
show "A * B:", d           # [0, 2, 8, 18, 32]
```

### Dot Product
```glow
let dot = tensor.dot(a, b)
show "A · B:", dot         # 0*0 + 1*2 + 2*4 + 3*6 + 4*8 = 60
```

---

## 3. Matrix Operations

### Matrix Multiplication
```glow
# 2x3 Matrix A
let A = tensor.create(6)
A[0] = 1.0
A[1] = 2.0
A[2] = 3.0
A[3] = 4.0
A[4] = 5.0
A[5] = 6.0

# 3x2 Matrix B
let B = tensor.create(6)
B[0] = 7.0
B[1] = 8.0
B[2] = 9.0
B[3] = 10.0
B[4] = 11.0
B[5] = 12.0

# C = A × B (result: 2x2)
let C = tensor.matmul(A, 2, 3, B, 3, 2)
show "A × B:", C    # [58, 64, 139, 154]
```

### Matrix Transposition
```glow
# Transpose A from 2x3 to 3x2
let At = tensor.transpose(A, 2, 3)
show "Transpose:", At    # [1, 4, 2, 5, 3, 6]
```

---

## 4. SIMD Performance

### Benchmarking Tensor Operations
```glow
show "SIMD Performance Benchmark"
show "=========================="

let size = 1000000   # 1 million elements

let t1 = tensor.zeros(size)
let t2 = tensor.zeros(size)

# Fill with data
let i = 0
while i < size:
    t1[i] = i * 0.001
    t2[i] = i * 0.002
    i = i + 1
end

# Benchmark addition
let start = os.time()
let result = tensor.add(t1, t2)
let elapsed = os.time() - start
show f"1M element add: {elapsed}s"

# Benchmark dot product
start = os.time()
let dp = tensor.dot(t1, t2)
elapsed = os.time() - start
show f"1M element dot: {elapsed}s"
show f"Dot product result: {dp}"
```

Typical results on modern hardware:
- **1M element add:** ~0.003s (SIMD-accelerated)
- **1M element dot:** ~0.002s

---

## 5. Building a Neural Network

### Layer Class
```glow
class Layer:
    action init(in_dim, out_dim):
        self.weights = tensor.create(in_dim * out_dim)
        self.bias = tensor.zeros(out_dim)
        self.in_dim = in_dim
        self.out_dim = out_dim

        # Initialize weights with small random-like values
        let i = 0
        while i < in_dim * out_dim:
            self.weights[i] = (i % 7) * 0.1 - 0.3
            i = i + 1
        end
    end

    action forward(input):
        # Simple matrix-vector multiply + bias
        let output = tensor.zeros(self.out_dim)
        let o = 0
        while o < self.out_dim:
            let sum = 0.0
            let j = 0
            while j < self.in_dim:
                sum = sum + input[j] * self.weights[o * self.in_dim + j]
                j = j + 1
            end
            output[o] = sum + self.bias[o]
            o = o + 1
        end
        return output
    end
end
```

### Simple Neural Network
```glow
class NeuralNet:
    action init():
        self.layer1 = Layer(10, 5)
        self.layer1.init(10, 5)
        self.layer2 = Layer(5, 3)
        self.layer2.init(5, 3)
        self.layer3 = Layer(3, 1)
        self.layer3.init(3, 1)
    end

    action predict(input):
        let h1 = self.layer1.forward(input)
        let h2 = self.layer2.forward(h1)
        let out = self.layer3.forward(h2)
        return out
    end
end

# Create and run the network
let net = NeuralNet()
net.init()

let input = tensor.zeros(10)
let i = 0
while i < 10:
    input[i] = i * 0.1
    i = i + 1
end

let start = os.time()
let prediction = net.predict(input)
let elapsed = os.time() - start

show "Prediction:", prediction
show f"Inference time: {elapsed}s"
```

---

## 6. Auto-Differentiation

### Dual Numbers for Forward-Mode AD
```glow
class Dual:
    action init(val, grad):
        self.val = val
        self.grad = grad
    end

    action add(other):
        let result = Dual(0, 0)
        result.init(self.val + other.val, self.grad + other.grad)
        return result
    end

    action mul(other):
        let result = Dual(0, 0)
        result.init(
            self.val * other.val,
            self.val * other.grad + self.grad * other.val
        )
        return result
    end

    action show_value():
        show f"Dual(val={self.val}, grad={self.grad})"
    end
end

# Compute f(x) = x^2 + 3x at x=2
# f'(x) = 2x + 3, so f'(2) = 7
let x = Dual(0, 0)
x.init(2.0, 1.0)    # x = 2, dx/dx = 1

let three = Dual(0, 0)
three.init(3.0, 0.0)  # constant

# f(x) = x * x + 3 * x
let x_squared = x.mul(x)
let three_x = three.mul(x)
let result = x_squared.add(three_x)

show "f(2) =", result.val      # 10.0 (2^2 + 3*2)
show "f'(2) =", result.grad    # 7.0  (2*2 + 3)
```

---

## 7. Data Processing Pipeline

### Feature Engineering
```glow
# Normalize a tensor to [0, 1] range
func normalize(data, size):
    let min_val = data[0]
    let max_val = data[0]

    let i = 1
    while i < size:
        when data[i] < min_val:
            min_val = data[i]
        end
        when data[i] > max_val:
            max_val = data[i]
        end
        i = i + 1
    end

    let range_val = max_val - min_val
    let normalized = tensor.zeros(size)

    i = 0
    while i < size:
        when range_val > 0:
            normalized[i] = (data[i] - min_val) / range_val
        otherwise:
            normalized[i] = 0.0
        end
        i = i + 1
    end

    return normalized
end

# Create sample data
let data = tensor.create(8)
data[0] = 10.0
data[1] = 25.0
data[2] = 5.0
data[3] = 40.0
data[4] = 15.0
data[5] = 30.0
data[6] = 20.0
data[7] = 35.0

let norm = normalize(data, 8)
show "Normalized:", norm
```

### Statistical Functions
```glow
func tensor_mean(t, size):
    let sum = 0.0
    let i = 0
    while i < size:
        sum = sum + t[i]
        i = i + 1
    end
    return sum / size
end

func tensor_variance(t, size):
    let mean = tensor_mean(t, size)
    let sum_sq = 0.0
    let i = 0
    while i < size:
        let diff = t[i] - mean
        sum_sq = sum_sq + diff * diff
        i = i + 1
    end
    return sum_sq / size
end

let values = tensor.create(5)
values[0] = 2.0
values[1] = 4.0
values[2] = 6.0
values[3] = 8.0
values[4] = 10.0

show "Mean:", tensor_mean(values, 5)          # 6.0
show "Variance:", tensor_variance(values, 5)  # 8.0
```

---

## 8. Best Practices

### Performance Tips
- ✅ Use `tensor.*` operations instead of manual loops — they use SIMD
- ✅ Use `tensor.zeros()` for large allocations — avoids GC pressure
- ✅ Batch operations: do matrix multiply instead of row-by-row dot products
- ✅ Pre-allocate tensors — avoid creating them in hot loops

### Memory Tips
- Tensors are allocated outside the GC heap (no GC pause impact)
- Large tensors should be reused rather than recreated
- Use `tensor.zeros()` over `tensor.create()` for deterministic initialization

### Architecture Tips
- Use classes to encapsulate layers and models
- Separate data loading, preprocessing, and inference into distinct functions
- Use `spawn` for parallel data loading while the model computes

---

*GlowLang AI/ML Guide v1.0*
