# Library benchmarks

Speed of the standard library modules written in GlowLang (`lib/*.glow`).
Each benchmark prints operations per second; run it with

```bash
bin/glowlang run benchmarks/libs/<name>_bench.glow
python3 benchmarks/libs/<name>_bench.py     # where there is one
```

All results below were measured on an Intel i7-14650HX, Linux under WSL 2,
with GlowLang built by `make` and Python 3.14.4. Each was run three times;
the runs agreed within 2% and the middle one is shown.

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
