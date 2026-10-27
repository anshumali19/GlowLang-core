# Contributing to GlowLang

Thank you for your interest in GlowLang! It is an Indian-origin programming language with
natural-language syntax, created by Anshumali Sharma and Anamika Mukherjee, and contributions
of every size are welcome:

* bug reports and fixes,
* performance work on the VM, JIT and garbage collector,
* standard-library functions and `lib/` modules,
* tooling, editor support and documentation,
* example programs and tests.

---

## How to contribute

1. **Fork** the repository on GitHub and clone your fork. Forking to contribute is explicitly
   allowed by the [LICENSE](LICENSE).
2. Create a branch: `git checkout -b fix/short-description`.
3. Build:
   * Windows: `.\build.bat -NoSign` (see `scripts\build-windows.ps1` for requirements)
   * Linux/macOS: `make`
4. Make your change, and add a test in `tests/` with its expected output in
   `tests/expected/<name>.out` (check the output by hand before recording it).
5. Run the test suite — it compares every program's output, not just its exit code:
   * Windows: `.\run_all_tests.bat`
   * Linux/macOS: `make test`
   For runtime changes, also run with the JIT off (`GLOW_NOJITEXEC=1`) and under GC stress
   (`GLOWLANG_GC_STRESS=1`).
6. Commit with a sign-off (see below) and open a pull request against `main`.

For large changes to the compiler, VM, JIT or GC, please open an issue or discussion first so
we can agree on the design before you spend time on it.

---

## Sign your commits (Developer Certificate of Origin)

Every commit must be signed off, which certifies the [Developer Certificate of Origin
1.1](https://developercertificate.org/): that you wrote the change, or otherwise have the right to
submit it under the project's license. Add the sign-off with `-s`:

```bash
git commit -s -m "Fix list index error message"
```

This adds a line such as `Signed-off-by: Your Name <you@example.com>` to the commit.

---

## License of contributions

GlowLang is licensed under the Apache License 2.0 with the Commons Clause and the GlowLang
Additional Terms ([LICENSE](LICENSE)). By contributing you agree that:

* your contribution is licensed under those same terms (Apache License 2.0, Section 5);
* you keep the copyright in your contribution;
* the contribution becomes part of GlowLang, which remains the work of Anshumali Sharma and
  Anamika Mukherjee, and which nobody — including contributors — may sell or patent.

---

## Pull request checklist

* Focused: one fix or feature per pull request; no unrelated reformatting.
* Tested: new behaviour has a test; the full suite passes.
* Documented: user-visible changes are reflected in `docs/` or the README. Documentation states
  only what the code really does — performance claims come from `benchmarks/run-benchmarks.ps1`.
* Matches the surrounding code style: naming, comment density and structure.

---

## Reporting bugs and security issues

* Bugs and feature requests: open a GitHub issue with a minimal `.glow` program that shows the
  problem, the output you expected and the output you got.
* Security vulnerabilities: do **not** open a public issue — follow [SECURITY.md](SECURITY.md).

---

## Community standards

Be respectful, patient and constructive. Harassment, abuse, spam or deliberately disruptive
behaviour may lead to being blocked from contributing.

Thank you for helping build GlowLang.
