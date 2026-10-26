# GlowLang on Windows x64

## Status

- `bin\glowlang.exe` is built with MSVC by `build.bat` (`scripts\build-windows.ps1`), statically
  linked (OpenSSL, SQLite, C runtime), and signed.
- The 129-program test suite passes with output comparison (`.\run_all_tests.bat`) in four modes:
  default, `GLOW_NOJITEXEC=1`, `GLOWLANG_GC_STRESS=1` and `GLOW_NO_SIMD=1`.
- Benchmarks against CPython: [benchmarks/RESULTS.md](benchmarks/RESULTS.md).
`glowlang.exe` builds clean with Clang / MSVC on Windows x64 and the full 117-test suite passes (100% pass rate), including all expected exit codes:
- exit 0 for the normal passing tests (including `test_spawn.glow`, `double_rec.glow`, etc.)
- exit 42 for `test_std.glow`
- exit 1 for the expected-failure cases:
  `loop_rec_test.glow`, `test_div0_stack.glow`, `test_error_stack.glow`,
  `test_jit_div0.glow`, `test_throw_stack.glow`, `test_import.glow`

## JIT (Microsoft x64 ABI)

`src/jit.c` emits code for both the Microsoft x64 and System V ABIs (`GLOW_JIT_WIN64` vs
`GLOW_JIT_SYSV`): argument registers RCX/RDX/R8/R9 plus a stack slot for the fifth argument,
callee-saved RBX/RBP/RDI/RSI/R12–R15, 32-byte shadow space and 16-byte alignment around helper calls,
and `FlushInstructionCache` after code is written.

## Recursion
Native Microsoft x64 ABI JIT execution is fully implemented, verified, and enabled on Windows.

### Implementation Details:
1. **ABI Calling Convention Abstraction Layer**:
   - Platform detection gates (`GLOW_JIT_WIN64` vs `GLOW_JIT_SYSV`) in `src/jit.c`.
   - Parameter register mapping:
     - **SysV (Linux/macOS)**: `RDI, RSI, RDX, RCX, R8, R9`
     - **Win64 (Windows)**: `RCX, RDX, R8, R9` + shadow slot `[rsp + 32]` for argument 5.
   - Dual-ABI prologue/epilogue generation preserving callee-saved registers (`RBX, RBP, RDI, RSI, R12, R13, R14, R15` on Win64).
   - 32-byte shadow space allocation (`sub rsp, 40` / `add rsp, 40`) before issuing C helper calls on Win64 to satisfy Microsoft x64 ABI and maintain 16-byte stack alignment.
   - Fixed REX prefixes on Win64 argument emitters (`0x4D` for R8/R9).
   - Direct `[r12 + reg * 8]` indexing from `vm_stack` ensuring JIT functions receive live parameters.
   - Dynamic `FlushInstructionCache` call on Windows.

Compiled code and the interpreter share the VM register file. When compiled code cannot continue,
the interpreter resumes the same frame instead of starting a nested C-level interpreter, and every
path that uses native stack per call checks the remaining stack first. Deep recursion therefore
gives exact results up to the 65,500-call limit with or without the JIT (`tests/regress_recursion.glow`:
`depth(50000)`, `ack(3, 8) = 2045`), and unbounded recursion raises a catchable error. Earlier builds
returned wrong values past about 2,000 compiled calls and crashed past about 20,000 interpreted ones.

The executable reserves a 512 MB main-thread stack (`/STACK:536870912,4194304`).

## Dispatch

MSVC has no computed `goto`, so the MSVC build dispatches with a `switch`. `-Compiler clang`
(clang-cl) enables computed-goto dispatch.
Fixes (all in `src/vm.c`):
- Added a `tail_depth` counter that increments on every tail-call frame
  reuse and resets on every real `DO_CALL` push / `OP_RETURN` pop.
  The tail-call path now throws "Maximum recursion depth exceeded" when
  `call_depth + internal_depth + tail_depth >= 65500`.
- `vm_call_jit_direct` no longer resets `call_depth` to 0, so it
  accumulates across JIT self-/mutual recursion and the `call_depth > 10`
  deopt guard correctly routes deep recursion back to the interpreter.
- Self-recursion from the interpreter (`vm_self_call_jit_or_recurse`)
  uses the JIT fast path only while `call_depth < 400`, otherwise
  dispatches directly through `run_bytecode` (single-frame recursion that
  hits the clean depth guard).
- The `func_get_specialization` re-attempt loop in `OP_CALL` /
  `OP_CALL_SELF_FAST` is guarded by `!fe->body->jit_failed`.

## Signing and Smart App Control

`scripts\sign-windows.ps1` signs with the `CN=GlowLangDev` certificate (SHA-256, DigiCert RFC 3161
timestamp). It is self-signed, so other machines do not trust it. Smart App Control judges each new
binary by reputation and has blocked some builds (including all clang-cl builds tried); rebuilding
gives a new file that is judged again. Shipping to other machines needs a trusted code-signing
certificate (for example Azure Trusted Signing).

## Crash reports

On an access violation the runtime prints a native backtrace, with function names and source lines
when `glowlang.pdb` sits next to the executable.
    # using Clang with AVX2 and MSYS2 libraries
    clang -mavx2 -O2 -I"src" -I"C:\msys64\ucrt64\include" ... -o bin\glowlang.exe

    # run the full regression test suite
    cmd /c run_all_tests.bat
    # expected: Total: 117, Passed: 117, Failed: 0

## Production Status

- Microsoft x64 ABI JIT backend is active and tested.
- `Fibonacci(30)` executes in ~14.5 ms.
- Multi-core `spawn` executes concurrently with zero GIL contention across independent thread stacks.
