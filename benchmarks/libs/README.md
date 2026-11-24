# Library benchmarks

Speed of the standard library modules written in GlowLang (`lib/*.glow`).
Each benchmark prints operations per second; run it with

```bash
bin/glowlang run benchmarks/libs/<name>_bench.glow
python3 benchmarks/libs/<name>_bench.py     # where there is one
```

All results below were measured on an Intel i7-14650HX, Linux under WSL 2,
with GlowLang built by `make` and Python 3.14.4. Each was run three times
and the middle run is shown; GlowLang's runs agreed within 3%.

## template

[template_bench.glow](template_bench.glow) renders a page with a 100-row
table (three values and a section per row). [template_bench.py](template_bench.py)
builds the same page by hand in Python; no Python template engine is
installed on this machine, so none is compared. Measured 2026-11-24.

| Way | Renders/s |
|---|---|
| `template.render` (parses every time) | 7,607 |
| `template.compile`, then `render` | 8,254 |
| GlowLang, built by hand | 20,879 |
| Python, built by hand | 35,526 |

A compiled template takes about 0.12 ms for this page, 2.5 times as long as
building it by hand in GlowLang.

## collections

[collections_bench.glow](collections_bench.glow) pushes 200,000 random
numbers onto a `Heap` and pops them all, makes 1,000,000 `push_back` and
`pop_front` pairs on a `Deque` holding 1,000 items, and counts 1,000,000
words. [collections_bench.py](collections_bench.py) does the same with
Python's `heapq`, `deque` and `Counter`, which are written in C. Python's
numbers varied by up to 25% between runs. Measured 2026-11-24.

| Work | GlowLang (ops/s) | Python (ops/s) |
|---|---|---|
| heap push and pop | 4,162,027 | 4,903,224 |
| deque push and pop | 24,047,426 | 46,670,012 |
| counting words | 17,965,297 | 39,983,451 |
