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
- **447 lines** of grammar rules covering all language constructs
- Precedence and associativity declarations for all operators
- Error handling productions for syntax reporting
- Inline compilation: AST nodes are constructed on an arena allocator and compiled to bytecode

### AST Construction & Arena Allocation
**Files:** `src/compiler.c`, `src/arena.c`

The AST represents program structure as a tree of nodes (`ASTNode`). Node memory is allocated using bump-pointer allocation in `src/arena.c`. The bytecode compiler (`src/compiler.c`) walks this tree to emit register bytecode and frees the arena in bulk upon compilation completion.

---

## 2. Bytecode Compiler

The compiler translates AST nodes into a flat array of bytecode instructions for the register-based VM. Key design decisions:

### Register Allocation
GlowLang uses a register-based (not stack-based) architecture:
- Every name a function assigns is a local for the whole function and gets a register before any temporary
- Temporaries are allocated above the locals and released after each expression
- A frame's size (`local_count`) is the highest register any instruction of the function uses

### Opcode Design
The full instruction set is the `OpCode` enum in `src/glowlang.h` (for example `OP_ADD`, `OP_LT`,
`OP_JUMP_IF_FALSE`, `OP_CALL`, `OP_CALL_SELF_FAST`, `OP_TAIL_CALL`, `OP_RETURN`, `OP_GET_PROPERTY`,
`OP_INDEX`, `OP_SPAWN`, `OP_THROW`, `OP_SIMD_ADD`).

---

## 3. AOT Optimizer

**File:** `src/opt.c`, `src/opt.h`

Before execution, bytecode passes through these optimizer passes (`optimize_bytecode`):

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

The pattern matcher recognizes only this `res[i] = a[i] + b[i]` shape. `OP_SIMD_ADD` uses AVX2 when the CPU has it; there is no NEON path.

---

## 4. Register-Based Virtual Machine

**File:** `src/vm.c`

The VM is the primary execution engine.

### Dispatch
Built with GCC or Clang, the loop dispatches with computed `goto` (`goto *dt[pc->op]`). MSVC has no
computed `goto`, so the MSVC build (the default on Windows) uses a `switch`. Each dispatch checks one
combined flag for the slow paths (pending exception, profiler, tracing, debugger).

### Tiering
A function is compiled by the baseline JIT after 5 executions and by the optimizing JIT after 50; a
type-specialized version is built after 10 calls with the same argument types. A loop is compiled
mid-execution (on-stack replacement) after 1,000 back-edge iterations.

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

Compiled code keeps every value in the same VM register file the interpreter uses, so
deoptimization needs no state reconstruction: compiled code stores the index of the current
instruction and returns a sentinel, and the interpreter continues from that instruction in the same
frame. Runtime helpers that cannot complete an operation exactly decline before doing any work, and a
raised exception also leaves compiled code this way.

### Cross-Platform Backends

| Target | Status | Notes |
|--------|--------|-------|
| x86-64 (System V) | ✅ Full | Linux / macOS AMD64 ABI; SSE/AVX2 SIMD support |
| x86-64 (Microsoft x64) | ✅ Full | Windows x64 ABI; 32-byte shadow space; 16-byte stack alignment |
| ARM64 | ❌ None | Interpreter only |
| WASM | ❓ Unverified | `make wasm` (Emscripten), interpreter only |

---

## 6. Memory Management

### Garbage Collector

**Architecture:** non-moving, stop-the-world mark-and-sweep.

- Objects smaller than a quarter block are bump-allocated in 1 MB blocks (with a per-block bitmap of
  object starts); larger objects are allocated individually.
- **Roots:** globals, constants, every thread's VM register stack, pending exceptions and the
  arguments of threads that have not started yet (precise), plus a conservative scan of every
  thread's native stack and saved registers. Interior pointers are recognized, so a value held only
  in a C local of a native function stays alive.
- **Trigger:** when allocated bytes exceed max(32 MB, 2 x the live size after the previous collection).
  `GLOWLANG_GC_STRESS=1` collects every 64 KB and `GLOW_FORCEFULL=1` on every allocation (testing only).
- **Generational mode** (`GLOW_GC_GENERATIONAL=1`) is experimental: a minor collection still walks the
  whole object list, so it saves little.
- Finalizers release native resources (SQLite connections and statements, tensor buffers).

### Guard pages
`src/arena.c` defines `glow_alloc_guarded(size)` (page-aligned allocation with a trailing
`PAGE_NOACCESS` / `PROT_NONE` page), but nothing in the runtime or `lib/` calls it yet.

### Arena Allocator
**File:** `src/arena.c`, `src/arena.h`

The arena allocator provides bump-pointer allocation for AST nodes and compiler temporaries:
- Allocation: O(1) pointer bump
- Deallocation: O(1) bulk free of entire arena
- No per-object free — the entire arena is released at once after compilation

### Object Model
Each instance stores its fields in its own dictionary. `OP_GET_PROPERTY` / `OP_SET_PROPERTY` cache
the slot index of the last field they found, so repeated access to the same field skips the hash
lookup. (Classes create `Shape` objects, but instances do not use them for layout.)

---

## 7. Data Structures

### Strings
Strings are flat, length-prefixed byte arrays. `a + b` builds a flat string when the result is at most
256 bytes; longer results become a rope node (an unbalanced pair of pieces), so building a long string
by repeated appends stays linear. A rope is flattened once, the first time its bytes are needed, and
the flat copy is kept. `len` is O(1).

### Packed Arrays (Tensors)
Tensors are contiguous arrays of `double` values, allocated outside the GC heap:
- Direct memory layout for SIMD alignment
- AVX2 (+FMA for dot and matmul) kernels are selected at run time by CPUID, with scalar fallbacks
- Tensor operations bypass the object system for maximum throughput

### Iterators
GlowLang provides lazy iterators for collections:
- `for each` loops use iterator protocol internally
- `range(n)` builds a list of `n` integers
- List comprehensions build results via iterators

---

## 8. Concurrency Model

### Threads and the GIL
`spawn` starts a native OS thread (`CreateThread` on Windows, `pthread_create` on POSIX). Threads share
globals and the heap, so a global interpreter lock (a FIFO ticket lock) lets exactly one thread run
GlowLang code at a time:

- A thread releases the lock during `os.sleep`, `os.system` and HTTP/TLS socket calls.
- A running thread hands the lock to a waiting one at loop back-edges and calls (compiled loops poll
  too).
- `threads.join_all()` waits for spawned threads; program exit waits too. `whenever` monitors are
  daemon threads and are not waited for.
- There is no parallel execution of GlowLang code; use threads to overlap waiting.

### Spawn Mechanics
```glow
spawn worker(arg1, arg2)
```

1. Bytecode compiler lowers `spawn` calls into `OP_SPAWN`, capturing target function address and argument registers.
2. The runtime packages the function and arguments into a `ThreadData` bundle.
3. An OS thread is spawned (`CreateThread` / `pthread_create`), launching `thread_wrapper()`.
4. `thread_wrapper()` provisions a dedicated VM stack, aligns argument registers, and takes the GIL and runs the target function.

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
3. Expose it in a module dictionary (`var_set("name", value_dict(...))` in the stdlib init); `module.function()` calls go through `OP_CALL_METHOD`

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
make              # bin/glowlang and bin/gdu (gcc -O2)
make test         # output-checked test suite
make clean        # remove build artifacts
```
On Windows, `build.bat` runs `scripts/build-windows.ps1` (MSVC `/O2`, static libraries, signed).

---

## 11. Performance Characteristics

Measured numbers, with the machine and method, are in
[benchmarks/RESULTS.md](../benchmarks/RESULTS.md) (generated by `benchmarks/run-benchmarks.ps1`).
On that machine the Windows release binary is about 11 MB (OpenSSL and SQLite are linked
statically) and starts and exits in about 13 ms.
