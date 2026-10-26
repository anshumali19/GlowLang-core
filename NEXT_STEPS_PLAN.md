# GlowLang — Plan for the Remaining Work

Status as of commit `002e116` on `feat/windows-jit-abi` (pushed to
`github.com/anshumali19/GlowLang-core`). What is already done and verified is in
[AUDIT_AND_FIX_PLAN.md](AUDIT_AND_FIX_PLAN.md). Nothing below has been started; each phase is done
only when asked, and each one ends with the test suite passing in all four modes and a commit.

---

## Phase 1 — Verify Linux and macOS (small)

The Makefile, CMake, CI and `scripts/run-tests.sh` were updated but never run.

1. Push the branch and read the first CI run (Linux build + tests, JIT-off and GC-stress runs, ASan,
   macOS build + tests, Windows CMake build + tests).
2. Fix what fails. Likely spots: `pthread_getattr_np` / `nanosleep` declarations, X11 linking,
   flex/bison versions, tests whose output differs by platform (paths, float formatting).
3. Optionally build locally in WSL (needs `sudo apt install gcc make flex bison libsqlite3-dev libssl-dev libx11-dev`).

Done when: CI is green on all three platforms.

## Phase 2 — Language behaviour gaps (small–medium)

1. **Integer typing:** integers between 2^47 and 2^53 report `type() == "float"`. Track "integral"
   separately (e.g. keep them as big integers or tag exact doubles) so `type()` says `int`.
2. **Out-of-range list index:** `l[99]` returns `none`; raise an `IndexError`-style exception
   (decide: error vs. `none`, since it changes behaviour of existing programs).
3. **`2 ** -1` parse error:** allow unary minus after `**`.
4. **`none` arithmetic:** `none * 2` gives `0` silently; raise a type error.
5. **Anonymous `func(...)...end` expressions:** either support them or keep `lambda` only and say so in
   the language reference.
6. Re-check `docs/language_reference.md`, `docs/getting_started.md` and the other guides by running
   every code sample, as was done for the README.

## Phase 3 — Repository hygiene (small)

1. Add `certs/`, `backups/`, `GlowLang-MSIX-Staging/`, `*.msix` to `.gitignore` (never commit the
   signing key).
2. Remove tracked build output (benchmark executables, `.class`, C# `bin/obj`, `editors/vscode/node_modules`)
   and root-level scratch files (`tmp_*`, stray `.py`, `.obj`, `.db`).
3. Delete leftover scratch binaries in `build_win/`.

## Phase 4 — Signing and distribution (small, needs your decision)

1. Choose a trusted signer (Azure Trusted Signing or a purchased OV/EV certificate) so Smart App
   Control and other machines accept the binary.
2. Wire it into `scripts/sign-windows.ps1` and add a release workflow that builds, signs and
   attaches `glowlang.exe` + `.pdb`.
3. Sign binaries produced by `glow package`.

## Phase 5 — Performance (medium–large)

Measure each step with `benchmarks/run-benchmarks.ps1`; keep a change only if RESULTS.md improves
and tests stay green.

1. Faster `split` and other string natives (single-char delimiter fast path, fewer allocations).
2. Cheaper call path from compiled code (avoid per-call helper and call-stack bookkeeping on
   self-recursion where safe).
3. JIT support for more opcodes (comparisons on mixed types, `OP_POW`, list/dict ops) to reduce
   side exits.
4. A real nursery (young generation with a remembered set) so minor GCs scale with live young data.
5. Make the clang-cl build (computed-goto dispatch) usable and compare it with MSVC.

## Phase 6 — Real multi-core execution (large, optional)

The GIL makes threads safe but serial. Two options:

- **Isolates:** each thread gets its own heap, globals and intern table; threads communicate by
  copying messages through channels. Safer, simpler to reason about.
- **Fine-grained locking:** shared heap with per-object/per-structure locks and GC safepoints.
  Harder; needs TSan-clean runs before any "multi-core" claim goes back into the docs.

Decide the model before starting; update the docs only after it works and is measured.

---

### Working rules

- One phase per branch/PR on GlowLang-core; commit after each verified step.
- Every fix gets a `tests/regress_*.glow` test with hand-checked expected output.
- Docs change only to describe measured, working behaviour.
