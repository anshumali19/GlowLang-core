# Library benchmarks

Speed of the standard library modules written in GlowLang (`lib/*.glow`).
Each benchmark prints operations per second; run it with

```bash
bin/glowlang run benchmarks/libs/<name>_bench.glow
python3 benchmarks/libs/<name>_bench.py     # where there is one
```

All results below were measured on an Intel i7-14650HX, Linux under WSL 2,
with GlowLang built by `make` and Python 3.14.4. Each was run three times
and the middle run is shown. The runs of template and collections in
GlowLang agreed within 3%; other sections say how far theirs differed.

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

## csv

[csv_bench.glow](csv_bench.glow) writes 100,000 rows of 5 cells with
`csv.stringify`, one row in ten with a cell that needs quotes, and reads
them back with `csv.parse`. [csv_bench.py](csv_bench.py) does the same with
Python's `csv` module, which is written in C. Runs differed by up to 15% in
GlowLang and 13% in Python. Measured 2026-11-24.

| Work | GlowLang (rows/s) | Python (rows/s) |
|---|---|---|
| writing | 1,129,701 | 3,978,096 |
| reading | 1,267,392 | 2,962,192 |

## toml

[toml_bench.glow](toml_bench.glow) parses
[toml_config.toml](toml_config.toml), a 65-line service configuration with
tables, arrays of tables, inline tables and a multi-line string.
[toml_bench.py](toml_bench.py) parses it with Python's `tomllib`, which is
also written in Python. Runs differed by under 2% in both. Measured
2026-11-24.

| Parser | Parses/s |
|---|---|
| GlowLang `toml.parse` | 4,646 |
| Python `tomllib.loads` | 7,454 |

Most of the time goes to reading the text one character at a time; one
parse of this file takes 0.22 ms.

## dotenv

[dotenv_bench.glow](dotenv_bench.glow) parses a 60-line `.env` text with
plain, double-quoted and commented values. python-dotenv is not installed
on this machine, so there is no Python column. Runs differed by under 1%.
Measured 2026-11-24.

| Parser | Parses/s |
|---|---|
| GlowLang `dotenv.parse` | 9,778 |

That is about 0.1 ms for the file; `dotenv.load` runs once when a program
starts.

## markdown

[markdown_bench.glow](markdown_bench.glow) renders
`docs/language_reference.md` (about 1,700 lines of headings, tables, lists
and code blocks) to HTML, and then one paragraph of 3,000 unmatched `*`.
No Python Markdown package is installed on this machine, so there is no
Python column. Runs differed by under 3%. Measured 2026-11-24.

| Work | Time |
|---|---|
| language_reference.md | 4.7 ms per render (15 MB/s) |
| 3,000 unmatched `*` | 3.1 ms |

The second line checks that text full of delimiters with no partner does
not take quadratic time.

## url

[url_bench.glow](url_bench.glow) times `url.parse` (cycling through 1,000
different links), `url.encode`, `url.query` and `url.join`.
[url_bench.py](url_bench.py) does the same with Python's `urllib.parse`,
which is written in Python; its parse also asks for the port and the decoded
query, as `url.parse` gives them. 1,000 links are used because `urlsplit`
caches its last 128 results. Runs differed by under 2%. Measured 2026-11-24.

| Work | GlowLang (calls/s) | Python (calls/s) |
|---|---|---|
| parse | 235,248 | 330,629 |
| encode | 267,388 | 1,446,273 |
| query string from a dict | 154,547 | 435,761 |
| join | 253,020 | 406,728 |

Python's `quote` turns the text into bytes and maps them through a table in
one call; `url.encode` looks at one character at a time.
