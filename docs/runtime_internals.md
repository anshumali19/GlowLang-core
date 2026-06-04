# GlowLang Runtime Internals

A deep-dive into the architecture and implementation of GlowLang's runtime — covering the compiler pipeline, virtual machine, JIT compiler, garbage collector, memory management, and native module system.

---

## Architecture Overview

```
┌─────────────────────────────────────────────────────────┐
│                    GlowLang Pipeline                     │
│                                                         │
│  Source Code                                            │
│      │                                                  │
│      ▼                                                  │
│  ┌──────────┐    ┌──────────┐    ┌────────────────┐    │
│  │  Lexer   │───▶│  Parser  │───▶│   Compiler     │    │
│  │  (flex)  │    │  (bison) │    │  (AST→Bytecode)│    │
│  └──────────┘    └──────────┘    └───────┬────────┘    │
│                                          │              │
│                                          ▼              │
│                                  ┌───────────────┐      │
│                                  │  AOT Optimizer│      │
│                                  │  (4 passes)   │      │
│                                  └───────┬───────┘      │
│                                          │              │
│                                          ▼              │
│  ┌──────────┐    ┌──────────┐    ┌───────────────┐      │
│  │  Native  │◀───│  JIT     │◀───│  Register VM  │      │
│  │  Code    │    │  Compiler│    │  (dispatch)   │      │
│  └──────────┘    └──────────┘    └───────────────┘      │
│                                          │              │
│                                          ▼              │
│                                  ┌───────────────┐      │
│                                  │ Generational  │      │
│                                  │     GC        │      │
│                                  └───────────────┘      │
└─────────────────────────────────────────────────────────┘
```

---

## 1. Frontend: Lexer and Parser

### Lexer (flex)
**File:** `src/glowlang.l`

The lexer tokenizes GlowLang source code using a flex-generated scanner. Key responsibilities:
- Recognizing keywords (`let`, `func`, `class`, `show`, `when`, `spawn`, etc.)
- Tokenizing string literals (including f-string interpolation)
- Handling comments (`#` single-line, `/* */` multi-line)
- Number literal parsing (integers and floating-point)
- Operator recognition

### Parser (bison)
**File:** `src/glowlang.y`

The parser transforms tokens into an Abstract Syntax Tree (AST). GlowLang uses a bison-generated LALR(1) parser with:
- **183K+ lines** of grammar rules covering all language constructs
- Precedence and associativity declarations for all operators
- Error recovery productions for meaningful error messages
- Inline compilation: AST nodes are compiled to bytecode during parsing

### AST Construction
**File:** `src/ast.c`

The AST represents program structure as a tree of nodes. Each node type corresponds to a language construct (variable declaration, function call, class definition, etc.). The compiler walks this tree to emit bytecode.

---

## 2. Bytecode Compiler

The compiler translates AST nodes into a flat array of bytecode instructions for the register-based VM. Key design decisions:

### Register Allocation
GlowLang uses a register-based (not stack-based) architecture:
- **256 virtual registers** per function frame
- Register allocation during compilation assigns temporaries to registers
- Eliminates stack push/pop overhead found in stack VMs

### Opcode Design
The instruction set includes operations for:

| Category | Example Opcodes |
|----------|----------------|
| Arithmetic | `OP_ADD`, `OP_SUB`, `OP_MUL`, `OP_DIV`, `OP_MOD` |
| Comparison | `OP_EQ`, `OP_LT`, `OP_GT`, `OP_LE`, `OP_GE` |
| Control | `OP_JMP`, `OP_JMP_IF_FALSE`, `OP_CALL`, `OP_RET` |
| Load/Store | `OP_LOAD_CONST`, `OP_LOAD_LOCAL`, `OP_STORE_LOCAL` |
| Objects | `OP_GET_PROP`, `OP_SET_PROP`, `OP_NEW_INSTANCE` |
| Collections | `OP_INDEX_GET`, `OP_INDEX_SET`, `OP_NEW_LIST`, `OP_NEW_DICT` |
| SIMD | `OP_SIMD_ADD`, `OP_SIMD_MUL`, `OP_SIMD_DOT` |
| Concurrency | `OP_SPAWN`, `OP_YIELD` |
| Exception | `OP_TRY_PUSH`, `OP_TRY_POP`, `OP_THROW` |

---

## 3. AOT Optimizer

**File:** `src/opt.c`, `src/opt.h`

Before execution, bytecode passes through a 4-pass ahead-of-time optimizer:

### Pass 1: Block Boundary Analysis
Identifies basic blocks and constructs a control flow graph (CFG). This enables subsequent passes to reason about data flow within and across blocks.

### Pass 2: Constant Folding
Evaluates constant expressions at compile time:
```glow
# Before optimization
let x = 3 + 4 * 2     # Emits: CONST 3, CONST 4, CONST 2, MUL, ADD

# After optimization
let x = 11             # Emits: CONST 11
```

### Pass 3: Scalar Replacement
Replaces redundant load/store pairs for local variables with direct register references. Eliminates unnecessary memory traffic.

### Pass 4: SIMD Auto-Vectorization
Detects loops iterating over PackedArrays (tensors) performing element-wise operations and collapses them into single SIMD instructions:
```glow
# Before: loop with N iterations of OP_ADD
every i from 0 to size - 1:
    result[i] = a[i] + b[i]
end

# After: single OP_SIMD_ADD instruction
```

This pass uses AVX2/SSE on x86-64 and NEON on ARM64.

---

## 4. Register-Based Virtual Machine

**File:** `src/run_bytecode_optimized.c`

The VM is the primary execution engine, running optimized bytecode through a direct-threaded dispatch loop.

### Direct-Threaded Dispatch
Instead of a `switch` statement (indirect dispatch), GlowLang uses **computed goto** for dispatch:

```c
// Conceptual dispatch mechanism
static void *dispatch_table[] = {
    &&op_add, &&op_sub, &&op_mul, ...
};

#define DISPATCH() goto *dispatch_table[*ip++]

op_add:
    regs[A] = regs[B] + regs[C];
    DISPATCH();
```

This eliminates branch misprediction overhead, yielding ~30% faster dispatch versus switch-based VMs.

### Execution Profiling
The VM instruments function entry points with execution counters. When a function's count exceeds the **JIT threshold** (default: 100 invocations), it is promoted to the JIT compiler.

### Call Frame Management
Each function call pushes a frame onto the call stack:
- Return address (instruction pointer)
- Base register offset
- Closure environment pointer
- Exception handler chain

### Tail Call Optimization (TCO)
The VM detects tail-call patterns and reuses the current frame, preventing stack overflow in recursive algorithms:
```glow
func factorial(n, acc):
    if n <= 1:
        return acc
    end
    return factorial(n - 1, n * acc)   # TCO kicks in
end
```

---

## 5. Adaptive JIT Compiler

**File:** `src/jit.c`, `src/jit.h`

GlowLang's JIT compiler translates hot bytecode functions into native machine code at runtime.

### JIT Pipeline

```
Hot Function Detected
        │
        ▼
┌───────────────┐
│ Type Profiling│  Record argument types from execution history
└───────┬───────┘
        │
        ▼
┌───────────────┐
│ Type           │  Generate specialized code for observed types
│ Specialization │  (e.g., int+int vs float+float vs string+string)
└───────┬───────┘
        │
        ▼
┌───────────────┐
│ Code Emission  │  Emit native x86-64 / ARM64 machine instructions
└───────┬───────┘
        │
        ▼
┌───────────────┐
│ PIC Caching   │  Cache compiled code keyed by type signature
└───────┬───────┘
        │
        ▼
  Native Execution
```

### Type Specialization
The JIT records the types of function arguments across invocations. It then generates code specialized for the most common type combinations:

| Type Signature | Generated Code |
|----------------|----------------|
| `(int, int)` | Native integer ADD instruction |
| `(float, float)` | SSE/AVX floating-point ADD |
| `(string, string)` | Rope concatenation call |

### Polymorphic Inline Caches (PICs)
Property access and method calls use PICs to cache resolved shapes:
1. First access: full lookup, cache the shape + offset
2. Subsequent accesses: compare shape, use cached offset (1-2 instructions)
3. Shape mismatch: fall back to full lookup, update cache

### Guard Checks and Deoptimization
JIT-compiled code includes **type guards** — lightweight checks that verify assumptions:
```
# Pseudocode for JIT-compiled function
guard: is arg0 an integer?
  → yes: use fast native ADD
  → no:  DEOPTIMIZE → fall back to VM interpreter
```

When a guard fails, the JIT performs **deoptimization**:
1. Reconstruct the interpreter state (registers, stack)
2. Transfer execution back to the VM
3. Mark the function for re-specialization with updated type info

### Cross-Platform Backends

| Target | Status | Notes |
|--------|--------|-------|
| x86-64 | ✅ Full | SSE/AVX2 SIMD support |
| ARM64 | ✅ Full | NEON SIMD support |
| WASM | ✅ Full | WebAssembly output for browser deployment |

---

## 6. Memory Management

### Generational Garbage Collector

**Architecture:** Two-generation mark-and-sweep with write barriers.

```
┌─────────────────────┐     ┌─────────────────────┐
│     Nursery (Gen 0) │     │   Old Gen (Gen 1)   │
│                     │     │                     │
│  New allocations    │────▶│  Survived objects   │
│  Short-lived objects│     │  Long-lived objects  │
│                     │     │                     │
│  Collected often    │     │  Collected rarely    │
└─────────────────────┘     └─────────────────────┘
```

**Nursery (Generation 0):**
- All new objects are allocated here
- Collected frequently with low-latency sweeps
- Objects that survive N collections are promoted to Old Gen

**Old Generation (Generation 1):**
- Long-lived objects
- Collected infrequently with full mark-and-sweep
- Write barriers track cross-generational references

### Arena Allocator
**File:** `src/arena.c`, `src/arena.h`

The arena allocator provides bump-pointer allocation for AST nodes and compiler temporaries:
- Allocation: O(1) pointer bump
- Deallocation: O(1) bulk free of entire arena
- No per-object free — the entire arena is released at once after compilation

### Object Model: Hidden Classes (Shapes)
GlowLang objects use a Hidden Class / Shapes system (similar to V8's approach):

1. Each object has a **shape pointer** describing its property layout
2. Adding a property creates a new shape (transition)
3. Objects with the same properties in the same order share the same shape
4. The JIT uses shapes for fast property access via known offsets

```
Shape A: { name: offset 0 }
    │
    ▼ (add 'age' property)
Shape B: { name: offset 0, age: offset 1 }
    │
    ▼ (add 'email' property)
Shape C: { name: offset 0, age: offset 1, email: offset 2 }
```

---

## 7. Data Structures

### Rope Strings
GlowLang strings are backed by a **Rope** data structure — a balanced binary tree of string fragments:

| Operation | Rope Complexity | Flat String Complexity |
|-----------|----------------|----------------------|
| Concatenation | O(log n) | O(n) |
| Index Access | O(log n) | O(1) |
| Substring | O(log n) | O(n) |
| Length | O(1) cached | O(1) |

Ropes excel at workloads with frequent concatenation (template rendering, log building, protocol construction).

### Packed Arrays (Tensors)
Tensors are contiguous arrays of `double` values, allocated outside the GC heap:
- Direct memory layout for SIMD alignment
- AVX2 / NEON instructions operate on 256-bit / 128-bit chunks
- Tensor operations bypass the object system for maximum throughput

### Iterators
GlowLang provides lazy iterators for collections:
- `for each` loops use iterator protocol internally
- `range()` returns a lazy range iterator
- List comprehensions build results via iterators

---

## 8. Concurrency Model

### Thread Architecture
GlowLang uses **OS-level threads** (pthreads on Linux/macOS) with a **Global Interpreter Lock (GIL)**:

```
┌────────────┐  ┌────────────┐  ┌────────────┐
│  Thread 1  │  │  Thread 2  │  │  Thread 3  │
│  (worker)  │  │  (worker)  │  │  (worker)  │
└─────┬──────┘  └─────┬──────┘  └─────┬──────┘
      │               │               │
      └───────┬───────┘───────┬───────┘
              │               │
       ┌──────▼──────┐  ┌─────▼─────┐
       │    GIL      │  │  I/O      │
       │  (CPU work) │  │  (no GIL) │
       └─────────────┘  └───────────┘
```

- **CPU-bound work:** One thread holds the GIL at a time
- **I/O operations:** GIL is released during `os.sleep()`, `http.*`, `sqlite.*`, etc.
- **`spawn`:** Creates a new OS thread that competes for the GIL

### Spawn Mechanics
```glow
spawn worker(arg1, arg2)
```

1. Allocate a new thread structure
2. Clone function arguments
3. Create OS thread via `pthread_create`
4. New thread acquires GIL when scheduled

---

## 9. Native Module System

GlowLang's standard library modules are implemented as native C functions exposed to the bytecode VM:

| Module | Implementation | Description |
|--------|---------------|-------------|
| `tensor` | C + SIMD intrinsics | Matrix/vector operations |
| `http` | C sockets | TCP/HTTP server and client |
| `sqlite` | libsqlite3 | SQLite database bindings |
| `redis` | C sockets | Redis client protocol |
| `json` | C parser | JSON encode/decode |
| `gui` | X11/Xlib | Windowing and graphics |
| `os` | POSIX | Time, sleep, filesystem |
| `math` | C libm | Mathematical functions |
| `collision` | C | Game physics utilities |
| `crypto` | OpenSSL | Hashing and encryption |
| `fs` | POSIX I/O | File system operations |

### Extending GlowLang
Native modules follow a registration pattern:
1. Implement the C function with the GlowLang calling convention
2. Register it in the VM's function table
3. The parser recognizes `module.function()` syntax and emits `OP_NATIVE_CALL`

---

## 10. Build System

**File:** `Makefile`

The build system compiles GlowLang in the following order:

1. `flex` generates the lexer from `src/glowlang.l`
2. `bison` generates the parser from `src/glowlang.y`
3. `gcc` compiles all C sources with optimization flags
4. Linking against `libsqlite3`, `libssl`, `libcrypto`, `libpthread`, `libX11`

### Build Targets
```bash
make              # Build the full GlowLang binary
make clean        # Remove build artifacts
make test         # Run the test suite
```

### Compiler Flags
Production builds use aggressive optimization:
```
-O3 -march=native -flto -fomit-frame-pointer
```

---

## 11. Performance Characteristics

| Metric | Value | Notes |
|--------|-------|-------|
| Peak IPS (JIT) | ~1.86 Billion | Instructions per second |
| Fibonacci(30) | 0.048s | Recursive, JIT-optimized |
| 1M Tensor Add | 0.003s | SIMD-accelerated |
| GC Pause (Nursery) | <1ms | Generational collection |
| Startup Time | <5ms | Lexer + parser + compile |
| Binary Size | ~200KB | Statically linked |

---

*GlowLang Runtime Internals v1.0*
