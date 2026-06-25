# GlowLang Sovereign Engineering & Syntax Rules

> [!IMPORTANT]
> These rules are mandatory for all coding assistants and subagents working on GlowLang repositories.

## 1. Strict GlowLang Syntax Constraints
- **NO Semicolons**: Never use semicolons (`;`) to end lines or separate statements. Lines terminate simply with a newline.
- **NO `import` Keyword**: GlowLang does not have an `import` keyword. Always use `use` (e.g., `use "kavach.glow"`).
- **NO `print` Keyword**: GlowLang does not have a `print` keyword. Always use `show` (e.g., `show "Result: ", val`).
- **File Extensions**: All GlowLang files must strictly use the `.glow` extension.

## 2. Professional Repository Architecture & Directory Norms
- **Benchmarks**: All benchmark scripts and multi-language performance comparison suites belong strictly in the root `benchmarks/` directory (never inside `project/benchmarks/` or random subfolders).
- **Core Source**: C VM and compiler engine code belongs in `src/`.
- **Standard Library**: Sovereign high-level `.glow` modules (`GlowKavach`, `GlowPrism`, `GlowVeda`, etc.) belong in `lib/`.
- **Unit & Integration Tests**: Regression test files belong in `tests/` or `project/tests/`.

## 3. Verification Mandate
- Always verify C and VM implementations prior to submitting or pushing code.
- Always ask explicit user permission before making architectural changes or performing pushes/merges on protected branches.
