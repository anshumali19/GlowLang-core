# GlowLang Missing Features Analysis

## 1. Module System Implementation Gap (use keyword)
* **Feature Name:** `use` (module/file import)
* **Subsystem Affected:** Compiler (Codegen) / Module Loader
* **Detection Method:** Static analysis of `src/glowlang.y` shows `USE` was only supported for `ID` (traits).
* **Status:** FIXED (by agent). unified `use` system implemented for files and traits. Recursive compilation and state saving/restoring added.

## 2. Global Loop Counter Conflict
* **Feature Name:** Loop variable scoping
* **Subsystem Affected:** Compiler (Codegen)
* **Detection Method:** `NODE_FOR_RANGE` uses `global_get_slot` for the loop variable.
* **Why it matters:** Prevents nested loops or recursive functions from using the same loop variable name (e.g., `i`).
* **Runtime Impact:** Inner loops overwrite outer loop counters, leading to infinite loops or premature termination.
* **Engineering Priority:** High.

## 3. every each Implementation Gap
* **Feature Name:** `every each` (for-each) loop
* **Subsystem Affected:** Compiler (Codegen)
* **Detection Method:** Grammar supports `NODE_FOR_EACH`, but `compile_stmt` lacks a case for it.
* **Why it is missing:** Development focused on `FOR FROM TO` range loops.
* **Why it matters:** Essential for iterating over lists (middlewares, tasks, routes).
* **Runtime Impact:** `every each` loops act as no-ops.
* **Ecosystem Impact:** High. Blocks common collection processing patterns.
* **Engineering Priority:** Critical.

## 4. Lambda Closure Gap (Local Variable Capture)
* **Feature Name:** Closures / Variable Capture
* **Subsystem Affected:** Compiler (Codegen) / VM
* **Detection Method:** `lambda` functions cannot access local variables from their defining scope. Attempting to use `m1` (a local) inside a lambda defined in the same function fails as the VM looks for `m1` in the global table.
* **Why it matters:** Essential for event handlers, middleware, and callbacks (e.g., `server.handle`).
* **Runtime Impact:** Callbacks cannot access state defined in the setup function.
* **Engineering Priority:** High.

## 5. Global Thread-Safe Queue (Lock-Free)
* **Feature Name:** `AtomicQueue` / `LockFreeStack`
* **Subsystem Affected:** Runtime / Concurrency
* **Detection Method:** Attempted implementation of `runtime/workers.glow` requires manual `os.sleep` and manual list slicing which is not thread-safe in the current VM.
* **Why it matters:** High-performance worker pools require non-blocking synchronization.
* **Runtime Impact:** Current implementation uses `spawn` which shares the GIL-protected list, leading to contention.
* **Engineering Priority:** Medium.
