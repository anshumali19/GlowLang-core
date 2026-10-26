# GlowLang on Windows x64

## Status

- `bin\glowlang.exe` is built with MSVC by `build.bat` (`scripts\build-windows.ps1`), statically
  linked (OpenSSL, SQLite, C runtime), and signed.
- The 129-program test suite passes with output comparison (`.\run_all_tests.bat`) in four modes:
  default, `GLOW_NOJITEXEC=1`, `GLOWLANG_GC_STRESS=1` and `GLOW_NO_SIMD=1`.
- Benchmarks against CPython: [benchmarks/RESULTS.md](benchmarks/RESULTS.md).

## JIT (Microsoft x64 ABI)

`src/jit.c` emits code for both the Microsoft x64 and System V ABIs (`GLOW_JIT_WIN64` vs
`GLOW_JIT_SYSV`): argument registers RCX/RDX/R8/R9 plus a stack slot for the fifth argument,
callee-saved RBX/RBP/RDI/RSI/R12–R15, 32-byte shadow space and 16-byte alignment around helper calls,
and `FlushInstructionCache` after code is written.

## Recursion

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

## Signing and Smart App Control

`scripts\sign-windows.ps1` signs with the `CN=GlowLangDev` certificate (SHA-256, DigiCert RFC 3161
timestamp). It is self-signed, so other machines do not trust it. Smart App Control judges each new
binary by reputation and has blocked some builds (including all clang-cl builds tried); rebuilding
gives a new file that is judged again. Shipping to other machines needs a trusted code-signing
certificate (for example Azure Trusted Signing).

## Crash reports

On an access violation the runtime prints a native backtrace, with function names and source lines
when `glowlang.pdb` sits next to the executable.
