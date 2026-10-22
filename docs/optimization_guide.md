# GlowLang Optimization Guide

A practical guide to writing high-performance GlowLang code — covering JIT optimization, SIMD utilization, memory management, profiling techniques, and common anti-patterns.

---

## Performance Model

Understanding GlowLang's execution tiers helps you write faster code:

```
Tier 0: Interpreter (VM)
    │ 100 invocations
    ▼
Tier 1: JIT Compiled (Type-Specialized)
    │ Guard failure
    ▼
Tier 0: Deoptimized → Re-profiled → Tier 1
```

| Tier | Speed | When |
|------|-------|------|
| VM Interpreter | 1× baseline | First 100 calls of any function |
| JIT Compiled | 10-50× faster | After warmup, with stable types |
| SIMD Tensor | 100-500× faster | Native tensor operations |

---

## 1. JIT Optimization

### Warming Up Hot Functions
The JIT compiles functions after ~100 invocations. Structure your code so hot paths warm up early:

```glow
# ✅ Good: hot function warms up quickly
func process_item(item):
    return item * 2 + 1
end

# Warmup loop
let i = 0
while i < 100:
    process_item(i)
    i = i + 1
end

# Now process_item is JIT-compiled for all subsequent calls
let start = os.time()
i = 0
while i < 1000000:
    process_item(i)
    i = i + 1
end
show "Elapsed:", os.time() - start
```

### Type Stability
The JIT generates specialized code for observed argument types. Keep argument types consistent:

```glow
# ✅ Good: always called with integers → one specialization
func add_ints(a, b):
    return a + b
end

add_ints(1, 2)        # int + int
add_ints(10, 20)      # int + int (same specialization, fast)

# ❌ Bad: called with different types → multiple specializations + deopt
func add_mixed(a, b):
    return a + b
end

add_mixed(1, 2)          # int + int
add_mixed(1.5, 2.5)      # float + float (new specialization)
add_mixed("a", "b")      # string + string (yet another specialization)
```

### Avoiding Deoptimization
Deoptimization happens when JIT guards fail. Minimize by:

```glow
# ✅ Good: predictable types in hot loop
let sum = 0
let i = 0
while i < 1000000:
    sum = sum + i       # Always int + int
    i = i + 1
end

# ❌ Bad: type changes mid-loop
let result = 0
let i = 0
while i < 1000000:
    when i > 500000:
        result = result + 1.0    # Switches to float! Causes deopt
    otherwise:
        result = result + 1      # Integer
    end
    i = i + 1
end
```

---

## 2. SIMD Optimization

### Use Tensor Operations
For numeric workloads, always prefer `tensor.*` builtins over manual loops:

```glow
let size = 1000000

# ❌ Slow: manual loop (interpreted, then JIT but still scalar)
let a = []
let b = []
let c = []
let i = 0
while i < size:
    append(a, i * 1.0)
    append(b, i * 2.0)
    append(c, a[i] + b[i])
    i = i + 1
end

# ✅ Fast: SIMD-accelerated tensor operation
let ta = tensor.zeros(size)
let tb = tensor.zeros(size)
i = 0
while i < size:
    ta[i] = i * 1.0
    tb[i] = i * 2.0
    i = i + 1
end
let tc = tensor.add(ta, tb)   # Single SIMD operation!
```

### SIMD Auto-Vectorization
The AOT optimizer detects vectorizable patterns in loops over tensors:

```glow
# This loop will be auto-vectorized by the optimizer
let size = 10000
let a = tensor.zeros(size)
let b = tensor.zeros(size)
let result = tensor.zeros(size)

every i from 0 to size - 1:
    result[i] = a[i] + b[i]    # Collapsed to OP_SIMD_ADD
end
```

### Matrix Operations
Use built-in matrix operations instead of nested loops:

```glow
# ❌ Slow: manual matrix multiply with 3 nested loops
# ✅ Fast: built-in SIMD matrix multiply
let C = tensor.matmul(A, 2, 3, B, 3, 2)
```

---

## 3. Memory Optimization

### Minimize Allocations in Hot Loops
```glow
# ❌ Bad: creates a new list every iteration
let i = 0
while i < 100000:
    let temp = [1, 2, 3]    # GC pressure!
    i = i + 1
end

# ✅ Good: allocate once, reuse
let temp = [0, 0, 0]
let i = 0
while i < 100000:
    temp[0] = 1
    temp[1] = 2
    temp[2] = 3
    i = i + 1
end
```

### Pre-allocate Collections
```glow
# ❌ Bad: repeated append causes reallocation
let results = []
let i = 0
while i < 10000:
    append(results, i * 2)
    i = i + 1
end

# ✅ Better: use list comprehension (single allocation)
let results = [i * 2 for each i in range(10000)]
```

### Use Tensors for Numeric Data
Tensors are allocated outside the GC heap — no GC pause impact:

```glow
# ❌ Lists of numbers: GC-managed objects
let data = [1.0, 2.0, 3.0, 4.0, 5.0]

# ✅ Tensors: contiguous memory, GC-free, SIMD-ready
let data = tensor.zeros(5)
data[0] = 1.0
data[1] = 2.0
data[2] = 3.0
data[3] = 4.0
data[4] = 5.0
```

---

## 4. String Optimization

### Rope String Advantage
GlowLang strings use Ropes — concatenation is O(log n), not O(n):

```glow
# ✅ Efficient in GlowLang: rope concatenation
let result = ""
let i = 0
while i < 10000:
    result = result + "x"    # O(log n) per concat thanks to rope
    i = i + 1
end
```

### F-Strings vs Concatenation
```glow
# Both are efficient, f-strings are more readable
let name = "GlowLang"
let version = 1

# Option A: concatenation
let s1 = "Welcome to " + name + " v" + version

# Option B: f-string (preferred for readability)
let s2 = f"Welcome to {name} v{version}"
```

---

## 5. Control Flow Optimization

### Short-Circuit Evaluation
```glow
# ✅ Put cheap checks first
when len(list) > 0 and list[0] == target:
    show "Found!"
end
```

### Loop Optimization
```glow
# ❌ Bad: len() called every iteration
let i = 0
while i < len(items):
    process(items[i])
    i = i + 1
end

# ✅ Good: cache the length
let n = len(items)
let i = 0
while i < n:
    process(items[i])
    i = i + 1
end
```

### Tail Recursion
The VM optimizes tail calls — prefer tail recursion for recursive algorithms:

```glow
# ✅ Tail-recursive: TCO prevents stack overflow
func factorial(n, acc):
    when n <= 1:
        return acc
    end
    return factorial(n - 1, n * acc)   # Tail position
end

show factorial(1000, 1)   # Works! TCO reuses the frame
```

---

## 6. Profiling

### Manual Timing
```glow
func benchmark(label, fn, iterations):
    let start = os.time()
    let i = 0
    while i < iterations:
        fn()
        i = i + 1
    end
    let elapsed = os.time() - start
    show f"{label}: {elapsed}s ({iterations} iterations)"
    show f"  Per iteration: {elapsed / iterations}s"
end

func work():
    let sum = 0
    let i = 0
    while i < 1000:
        sum = sum + i
        i = i + 1
    end
end

benchmark("Sum 1000", work, 10000)
```

### Comparing Approaches
```glow
show "=== Performance Comparison ==="

# Approach 1: Manual loop
let start = os.time()
let sum1 = 0
let i = 0
while i < 1000000:
    sum1 = sum1 + i
    i = i + 1
end
let time1 = os.time() - start
show f"Manual loop: {time1}s"

# Approach 2: List comprehension
start = os.time()
let nums = [x for each x in range(1000)]
let time2 = os.time() - start
show f"List comp: {time2}s"
```

---

## 7. Object-Oriented Performance

### Hidden Class Stability
Objects with the same property assignments in the same order share a hidden class. Keep initialization consistent:

```glow
# ✅ Good: consistent property order → shared hidden class
class Point:
    func init(x, y):
        self.x = x     # Always x first
        self.y = y     # Always y second
    end
end

# All Point instances share one hidden class
let p1 = Point(1, 2)
p1.init(1, 2)
let p2 = Point(3, 4)
p2.init(3, 4)

# ❌ Bad: different property orders → different hidden classes
class BadPoint:
    func init(x, y, label):
        when label is not none:
            self.label = label   # Sometimes first
        end
        self.x = x
        self.y = y
    end
end
```

### Inline Caches
Property access is cached via Polymorphic Inline Caches (PICs). Accessing the same property on objects with the same shape is near-instant:

```glow
# ✅ Fast: monomorphic access (all same shape)
let points = []
let i = 0
while i < 1000:
    let p = Point(0, 0)
    p.init(i, i * 2)
    append(points, p)
    i = i + 1
end

for each p in points:
    let d = p.x + p.y   # PIC cached after first hit
end
```

---

## 8. Concurrency Performance

### Multi-Core Parallelism: Spawn Freely
With the Global Interpreter Lock (GIL) abolished, `spawn` executes tasks across multiple CPU cores in true parallel without lock serialization:

```glow
# ✅ True CPU parallelism: runs simultaneously on multiple hardware cores
spawn compute_shard(partition1)
spawn compute_shard(partition2)
spawn compute_shard(partition3)
```

### Vectorized Math: Combine Concurrency with SIMD
For high-density numeric operations, combine multi-core worker threads with hardware AVX2 SIMD tensor operations:

```glow
# ✅ Vectorized SIMD matrix multiplication
let result = tensor.matmul(A, m, k, B, k, n)
```

---

## 9. Common Anti-Patterns

| Anti-Pattern | Problem | Fix |
|-------------|---------|-----|
| Type-unstable functions | JIT deoptimization | Keep argument types consistent |
| Allocating in hot loops | GC pressure | Pre-allocate, reuse objects |
| Manual numeric loops | Misses SIMD | Use `tensor.*` builtins |
| Deep recursion without TCO | Stack overflow | Use tail recursion or iteration |
| Uncached `len()` in loop condition | Redundant computation | Cache to a local variable |
| String building without rope awareness | N/A in GlowLang | Ropes make concat efficient |
| Inconsistent property initialization | Multiple hidden classes | Keep property order consistent |

---

## 10. Performance Benchmarks

Reference numbers on modern hardware (x86-64, ~3.5 GHz):

| Benchmark | Time | Notes |
|-----------|------|-------|
| Fibonacci(30) recursive | 0.048s | JIT-optimized |
| 1M integer sum | 0.005s | JIT tight loop |
| 1M tensor add | 0.003s | SIMD AVX2 |
| 1M tensor dot product | 0.002s | SIMD AVX2 |
| 100K dict lookups | 0.01s | Hidden class + PIC |
| 10K string concats | 0.001s | Rope O(log n) |
| HTTP request/response | <1ms | Native sockets |
| SQLite INSERT | <0.1ms | Native binding |

---

*GlowLang Optimization Guide v1.0*
