# GlowAI Tutorial — Machine Learning in GlowLang

## Introduction

GlowAI provides neural network building blocks on top of GlowLang's native tensor engine.
All tensor operations use SIMD-accelerated C code via the `tensor.*` native API.

## Getting Started

### Creating Tensors
```
# Fixed-size tensor filled with zeros
let data = tensor.zeros(10)

# Create and fill manually
let weights = tensor.create(5)
tensor.set(weights, 0, 0.5)
tensor.set(weights, 1, -0.3)
tensor.set(weights, 2, 0.8)
tensor.set(weights, 3, -0.1)
tensor.set(weights, 4, 0.6)
```

### Tensor Operations
```
let a = tensor.zeros(5)
let b = tensor.zeros(5)

# Fill with data
let i = 0
while i < 5:
    tensor.set(a, i, i * 1.0)
    tensor.set(b, i, i * 2.0)
    i = i + 1
end

# Element-wise operations
let c = tensor.add(a, b)    # [0, 3, 6, 9, 12]
let d = tensor.mul(a, b)    # [0, 2, 8, 18, 32]

# Dot product
let dot = tensor.dot(a, b)  # 0+2+8+18+32 = 60
```

## Building Neural Networks

### Layer Blueprint
```
use "lib/ai.glow"

blueprint Layer:
    action init(in_dim, out_dim):
        self.weights = tensor.create(in_dim * out_dim)
        self.bias = tensor.zeros(out_dim)
    end

    action forward(input):
        return tensor.add(tensor.dot(input, self.weights), self.bias)
    end
end
```

### Simple Neural Network
```
blueprint SimpleNet:
    action init():
        self.l1 = Layer(10, 5)
        self.l2 = Layer(5, 1)
    end

    action predict(input):
        let x = self.l1.forward(input)
        return self.l2.forward(x)
    end
end

let net = SimpleNet()
let input = tensor.zeros(10)
let output = net.predict(input)
show "Prediction: ", output
```

## Tensor Utilities

The `lib/tensor_utils.glow` library provides higher-level operations:

```
use "lib/tensor_utils.glow"

# Statistical operations
let data = tensor.create(5)
tensor.set(data, 0, 1.0)
tensor.set(data, 1, 2.0)
tensor.set(data, 2, 3.0)
tensor.set(data, 3, 4.0)
tensor.set(data, 4, 5.0)

let mean = tensor_mean(data, 5)     # 3.0
let max_v = tensor_max(data, 5)     # 5.0
let min_v = tensor_min(data, 5)     # 1.0

# Activation functions
let relu_out = tensor_relu(data, 5)
let sigmoid_out = tensor_sigmoid(data, 5)
let softmax_out = tensor_softmax(data, 5)

# Matrix operations (row-major flat tensors)
let mat_a = tensor.create(6)  # 2x3 matrix
let mat_b = tensor.create(6)  # 3x2 matrix
let result = mat_multiply(mat_a, mat_b, 2, 3, 2)  # 2x2 result
```

## Performance Tips

1. **Use tensor.zeros() for large allocations** — pre-allocates contiguous memory
2. **SIMD acceleration** — tensor.add(), tensor.mul() use AVX2/SSE4.2 for large tensors
3. **Batch operations** — process multiple samples at once rather than one-by-one
4. **Minimize tensor creation** — reuse tensors where possible to reduce GC pressure
