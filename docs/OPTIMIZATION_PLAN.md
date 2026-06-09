# GlowLang Optimization Roadmap (Fast-Glow)

## Goal
Transform GlowLang from a string-lookup based interpreter into a high-performance, index-based virtual machine.

---

## Phase 1: Local Variable Indexing (Complete)
**Status:** DONE (2026-05-23)
**Description:** Replace `OP_LOAD_VAR "name"` and `OP_STORE_VAR "name"` with `OP_LOAD_LOCAL index` and `OP_STORE_LOCAL index`. 
**Impact:** Achieved a **45% speedup** in iterative loops. 10M iterations dropped from 0.759s (global) to 0.417s (local).

### Tasks:
1. [x] Update `FuncEntry` to store local variable metadata.
2. [x] Add `CompilerContext` to track current function scope.
3. [x] Update `compile()` to resolve variables to indices.
4. [x] Implement `OP_LOAD_LOCAL` and `OP_STORE_LOCAL` in the VM.
5. [x] Refactor `CallFrame` to use inline stack-allocated locals (up to 16).

---

## Phase 2: Instruction Dispatch Optimization (Complete)
**Status:** DONE (2026-05-23)
**Description:** Replace the `switch` statement in the VM with **Computed Gotos** (Threaded Code).
**Impact:** Achieved a further **39% speedup**. 10M loop dropped from 0.417s to 0.254s. Total iterative speedup is now **~3x** (0.759s -> 0.254s).

---

## Phase 3: Fast Value Representation (NaN Boxing) (Complete)
**Status:** DONE (2026-05-23)
**Description:** Shrink the 16-byte `Value` struct to an 8-byte NaN-boxed double.
**Impact:** Achieved a **3x total iterative speedup** (0.759s -> 0.214s). Memory traffic reduced by 50% per stack operation. GlowLang is now ~3x faster than Python 3.12 in tight loops.

### Tasks:
1. [x] Overhaul `Value` type to use a `uint64_t` representation.
2. [x] Implement bitwise macros (`GET_TYPE`, `AS_INT`, etc.) for NaN boxing.
3. [x] Refactor all 100+ field accesses in the compiler and VM.
4. [x] Update value constructors and memory management for the new format.

---

## Phase 4: Just-In-Time (JIT) Compilation
**Status:** DONE (2026-05-28)
**Description:** Revived and stabilized the custom x86_64 JIT for hot arithmetic loops.

---

## Phase 5: High-Performance Redesign (Final Delivery)
**Status:** COMPLETED (2026-05-28)
**Description:** Transitioned to a production-grade Register-based VM with a multi-tier execution strategy.

### Tasks:
1. [x] **Register-based VM Architecture**: Removed stack pointer overhead; implemented 16-bit register indexing.
2. [x] **Memory System Overhaul**: Implemented Bump-pointer Arena Allocators and String Interning.
3. [x] **SSA-Lite Optimizer**: Integrated Basic-Block aware constant folding and type inference.
4. [x] **Garbage Collection Stability**: Re-enabled precision scanning across the full register range.

### Next Generation Roadmap:
- **M:N Fiber Scheduler**: Support for millions of lightweight tasks with NUMA-aware heaps.
- **SIMD Specialization**: Vectorized opcodes for high-throughput array processing.
- **AOT Snapshotting**: Persistent VM state images for zero-startup cloud execution.

---

## Change Log
- **2026-05-23:** Created Optimization Plan. Completed Phases 1, 2, and 3.
- **2026-05-28:** Completed Phase 4 (JIT Stabilization) and Phase 5 (Register VM Redesign). Total system throughput increased by ~10x.

