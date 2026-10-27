# GlowLang Audit & Fix Status (updated 2026-10-27)

Branch: `feat/windows-jit-abi` (based on `361670c`). Everything below was measured on Windows x64
(Intel i7-14650HX) with the MSVC build from `build.bat`. CI builds and runs the same suite on
Linux (GCC, plus an ASan run) and macOS (ARM64, Clang).

## Verification

- 135 test programs, each with expected stdout in `tests/expected/` (`scripts/run-tests.ps1`,
  `scripts/run-tests.sh`). All pass in four modes: default, `GLOW_NOJITEXEC=1`,
  `GLOWLANG_GC_STRESS=1`, `GLOW_NO_SIMD=1`.
- Stress: regression and thread tests give correct output with `GLOW_FORCEFULL=1` (a collection on
  every allocation); `test_stress_suite` 0 failures in 80 runs under GC stress.
- Benchmarks: `benchmarks/RESULTS.md` (all rows faster than CPython 3.13, 1.5x–5.2x).

## Bugs fixed

| Bug | Fix |
|---|---|
| Integers over 2^31 printed wrong on Windows | 64-bit formatting everywhere |
| Dicts broke past 65,536 string keys (38 s for 200k, then crash) | Resizable intern table with stored hashes |
| `spawn` crashed (no real GIL on Windows, none taken on POSIX) | Real FIFO GIL on all platforms, conservative stack scanning of all threads, `threads.join_all()` |
| Regex was a stub on Windows | `src/regex.c`, same engine everywhere |
| Assignment in functions wrote globals (mergesort wrong) | Python-like function scope; all assigned names hoisted to locals |
| Variable read before its assignment in a loop read a global | Same hoisting |
| Deep recursion: wrong results past ~2,000 compiled calls, crash past ~20,000 interpreted | Interpreter resumes compiled frames in place; native-stack checks; per-activation frame slices |
| Nested interpreter activations overwrote each other's frames | Frame pool sliced per activation |
| Frame size ignored temporaries freed mid-function | `local_count` = peak register use |
| Imported modules' top-level variables were invisible | Modules compile as top-level code |
| `use` inside deep recursion reported "circular import" | Separate import-depth counter |
| SQLite connection closed twice (crash at exit) | `close` clears the handle; finalizer uses `close_v2` |
| `0.0` printed as `none` | Removed the raw-zero special case |
| `os.sleep(0.01)` slept forever; `whenever` never fired and kept programs alive | Float-aware sleep; `whenever` monitors are daemon threads |
| `2 ** 60` inexact | Exact integer powers |
| AVX intrinsics without CPUID check | Runtime dispatch, scalar fallback |
| Tests passed on exit code only | Output-checked runners |

## Speed work

Loop OSR JIT, overflow-checked JIT arithmetic, slimmer dispatch, flat short-string concatenation,
O(1) `len`, single-copy rope flattening, FNV hashing, FMA kernels.

## Docs

README, ARCHITECTURE, PRODUCTION_GUIDE, WINDOWS_JIT_PORT, `docs/runtime_internals.md`,
`docs/concurrency_guide.md`, `docs/optimization_guide.md` and the benchmark reports were corrected:
no "GIL abolished", no concurrent/nursery GC, no MMU guard pages in use, no 14.5 ms Fibonacci,
switch dispatch stated for MSVC, hand-typed benchmark tables marked historical.

## Language work since the audit

Lists/dicts compare by contents; ordering works for text and lists; arithmetic and comparisons on
incompatible types raise; floor modulo; out-of-range indexes raise; `type()` reports `int` for
all integers; `x in xs`, `2 ** -1`, `1e3`, multi-line literals, `for each key, value in`;
expression f-strings; closures for nested functions and assignable captures; `input`/`ask`;
a core library (`src/corelib.c`) of string, list and dict methods and global helpers;
`lib/async.glow`; imports relative to the importing file.

## Still open

- Real multi-core execution (isolates or fine-grained locking) — not started.
- A real nursery GC.
- Closures capture copies; the enclosing function does not see a closure's assignments.
- Self-signed certificate: Smart App Control may block fresh builds; a trusted code-signing
  certificate is needed to ship to other machines.
- Repo hygiene: tracked build outputs and scratch files at the root.
