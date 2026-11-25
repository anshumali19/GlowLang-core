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
| writing | 2,395,933 | 4,007,056 |
| reading | 2,347,117 | 2,975,705 |

Measured again on 2026-11-25 (runs within 5% in GlowLang, 4% in Python)
after two changes. `stringify` joins a whole row and looks at its cells one
by one only when the line has a quote or line break, or more separators
than cells; `parse` splits a line with quotes at the quotes instead of
reading it a character at a time. Before, on the same machine that day,
writing ran at 1,217,149 rows/s and reading at 1,278,179.

## toml

[toml_bench.glow](toml_bench.glow) parses
[toml_config.toml](toml_config.toml), a 65-line service configuration with
tables, arrays of tables, inline tables and a multi-line string.
[toml_bench.py](toml_bench.py) parses it with Python's `tomllib`, which is
also written in Python. Runs differed by under 2% in both. Measured
2026-11-24.

| Parser | Parses/s |
|---|---|
| GlowLang `toml.parse` | 7,749 |
| Python `tomllib.loads` | 7,479 |

Measured again on 2026-11-25 (runs within 3%). `toml.parse` first splits
the text into tokens with one regular expression and reads the tokens;
it gives up on anything that reader does not cover (an escape, a mistake)
and reads the text character by character instead, as it did for every
file before, when it ran at 4,488 parses/s on the same machine that day.
One parse of this file now takes 0.13 ms.

The same benchmark then writes the parsed dict back with `toml.stringify`:
13,105 times a second, 0.08 ms each (runs differed by under 4%, measured
2026-11-25). Python's standard library has no TOML writer and `tomli_w` is
not installed on this machine, so there is no Python figure.

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
| encode | 8,274,786 | 1,446,273 |
| query string from a dict | 955,402 | 435,761 |
| join | 253,020 | 406,728 |

Since 2026-11-25 (measured that day, runs within 8%) `url.encode` calls
`http.url_encode`, which encodes the bytes in C as `http.url_decode`
decodes them; before, a loop over the characters in GlowLang gave encode
475,246 calls/s and query 180,761.

## semver

[semver_bench.glow](semver_bench.glow) times `semver.parse`,
`semver.compare`, `semver.satisfies` and sorting 1,000 random versions.
[semver_bench.py](semver_bench.py) does the same with Python's `packaging`
library (written in Python), which follows PEP 440 rather than semver but
does the same work. Runs differed by under 3% in GlowLang and 9% in Python.
Measured 2026-11-24.

| Work | GlowLang (calls/s) | Python (calls/s) |
|---|---|---|
| parse | 357,673 | 686,684 |
| compare | 303,950 | 442,339 |
| in a range | 67,593 | 84,529 |
| sort 1,000 | 558 | 578 |

The GlowLang column was measured again on 2026-11-25 (runs within 3%),
after two changes: parse takes the regular expression's groups as one list
instead of calling `group()` five times (parse was 288,078, compare
222,469), and `semver.sort`, like `packaging`, builds a key once per
version and sorts by the keys with the built-in `sorted` instead of
comparing field by field in a merge sort (it was 228).

## log

[log_bench.glow](log_bench.glow) writes 100,000 info lines with two fields
to a file, then makes 1,000,000 debug calls that the level skips.
[log_bench.py](log_bench.py) does the same with Python's `logging` module
(written in Python) and a FileHandler with the same kind of line. Runs
differed by under 6% in GlowLang and 8% in Python. Measured 2026-11-24.

| Work | GlowLang | Python |
|---|---|---|
| info line to a file | 277,295 lines/s | 208,212 lines/s |
| skipped debug call | 7,292,678 calls/s | 14,524,168 calls/s |

`log` opens the file for each line (`fs.append`); Python keeps it open.

## lru

[lru_bench.glow](lru_bench.glow) makes 1,000,000 lookups on a cache of
1,000 items with keys drawn from 2,000, so about half miss and are then set,
dropping the oldest item. [lru_bench.py](lru_bench.py) does the same with
the usual Python LRU: an `OrderedDict` (written in C) with `move_to_end`
and `popitem`. Runs differed by under 4% in both. Measured 2026-11-24.

| Cache | Lookups/s |
|---|---|
| GlowLang `lru.Cache` | 3,362,701 |
| Python, `OrderedDict` | 6,092,838 |

The GlowLang row was measured again on 2026-11-25 (runs within 2%), after
`get` stopped calling a helper method; before, it ran at 3,093,130 on the
same machine that day. A hit is still one remove and one re-insert in a
dict, as `OrderedDict` does in C.

## yaml

[yaml_bench.glow](yaml_bench.glow) parses this repository's
`.github/workflows/ci.yml` (191 lines). [yaml_bench.py](yaml_bench.py)
parses it with PyYAML 6.0.3: its `SafeLoader` (written in Python) and its
`CSafeLoader` (libyaml, written in C). All three give the same values,
except that PyYAML reads the key `on` as `True` (YAML 1.1). Runs differed
by under 2%. Measured 2026-11-24.

| Parser | Parses/s |
|---|---|
| GlowLang `yaml.parse` | 1,838 |
| PyYAML `SafeLoader` | 280 |
| PyYAML `CSafeLoader` | 3,419 |

The same benchmarks then write the parsed value back as text: GlowLang with
`yaml.stringify`, PyYAML with `yaml.dump(..., sort_keys=False)` and its
`SafeDumper` (Python) and `CSafeDumper` (C). PyYAML reads GlowLang's text
back as the same value, apart from the key `on`, which GlowLang writes in
quotes. Runs differed by under 3%. Measured 2026-11-25.

| Writer | Writes/s |
|---|---|
| GlowLang `yaml.stringify` | 5,173 |
| PyYAML `SafeDumper` | 381 |
| PyYAML `CSafeDumper` | 3,677 |

## schema

[schema_bench.glow](schema_bench.glow) validates an API request body (a
user with tags and an address) against a JSON Schema,
once with valid data and once with data that has six problems.
[schema_bench.py](schema_bench.py) does the same with Python's `jsonschema`
4.19.2 (written in Python), its validator built once, collecting every
error as `schema.validate` does. Runs differed by under 4%. Measured
2026-11-24.

| Data | GlowLang (validations/s) | Python (validations/s) |
|---|---|---|
| valid | 75,199 | 35,928 |
| six problems | 88,504 | 24,881 |
| formats | 102,001 | 109,518 |

The formats row (measured 2026-11-25, runs within 2%) checks an event with
a `uuid`, a `date-time`, an `email` and an `ipv4` text; Python uses a
`FormatChecker`. Python does less of the work: without the
`rfc3339-validator` package, which is not installed here, it passes
`date-time` unchecked, and it checks `email` only for an `@`.

Since 2026-11-25 (runs within 6%) each format is one regular expression,
with the numbers checked on its groups, instead of splitting the text and
matching each part; the formats row was 62,374 before.

## diff

[diff_bench.glow](diff_bench.glow) makes the unified diff of a 5,000-line
text against a copy with 50 lines changed, inserted or removed.
[diff_bench.py](diff_bench.py) makes it with Python's `difflib.unified_diff`
(written in Python). `diff.unified` finds a shortest diff; `difflib` uses
its own matching, which may not be. Runs differed by under 2%. Measured
2026-11-24.

| Function | Diffs/s |
|---|---|
| GlowLang `diff.unified` | 857.9 |
| Python `difflib.unified_diff` | 119.0 |

Large changes, measured 2026-11-25 with `/usr/bin/time`: a 20,000-line text
against a copy with one line in ten changed and one in ten removed. Up to
1.13.0, `diff.lines` gave up past 2,000 edits and reported all 37,922 lines
as removed and added, with a peak of 679 MB. Since then a change of more
than 200 edits is split around its middle (Myers' linear-space search):
it finds a 6,012-edit diff in 0.92 s with a peak of 133 MB. On the
50-edit benchmark above the two run within 3% of each other.

## cron

[cron_bench.glow](cron_bench.glow) times `cron.parse` of
`*/15 9-17 * * mon-fri`, and `next` from a Wednesday evening for that
schedule (the next morning) and for `0 0 1 1 *` (the next New Year).
`next` moves field by field, a month, a day or an hour at a time, rather
than minute by minute. croniter is not installed on this machine, so there
is no Python column. Runs differed by under 3%. Measured 2026-11-25.

| Work | Calls/s |
|---|---|
| parse | 204,219 |
| next, office hours | 117,127 |
| next, once a year | 595,711 |

## ini

[ini_bench.glow](ini_bench.glow) parses
[ini_config.ini](ini_config.ini), 74 lines in six sections with comments
and a value that goes on over two lines. [ini_bench.py](ini_bench.py)
parses it with Python's `configparser` (written in Python), keys kept in
their case and interpolation off, as `ini.parse` does. Both give the same
values. Runs differed by under 2%. Measured 2026-11-25.

| Parser | Parses/s |
|---|---|
| GlowLang `ini.parse` | 26,717 |
| Python `configparser` | 5,228 |

## retry

[retry_bench.glow](retry_bench.glow) measures what `retry.call` adds to a
call: a function that works at once, called directly and through
`retry.call`, and one that fails twice before it works, with no waits.
`tenacity` is not installed on this machine, so there is no Python column.
Runs differed by under 4%. Measured 2026-11-25.

| Call | Calls/s |
|---|---|
| directly | 17,773,049 |
| through `retry.call` | 1,041,001 |
| works on the third try | 278,904 |

`retry.call` adds about 0.9 microseconds to a call, most of it checking the
options.

## textwrap

[textwrap_bench.glow](textwrap_bench.glow) wraps a 2,000-word text to 70
columns and shortens a 40-word sentence to 60 characters.
[textwrap_bench.py](textwrap_bench.py) does the same with Python's
`textwrap` (written in Python); its words come from Python's random numbers
but have the same lengths. On 300 random texts both give the same lines.
Runs differed by under 2%. Measured 2026-11-25.

| Work | GlowLang (calls/s) | Python (calls/s) |
|---|---|---|
| wrap 2,000 words | 3,268.6 | 762.3 |
| shorten | 232,718.2 | 38,559.9 |

## humanize

[humanize_bench.glow](humanize_bench.glow) times `humanize.bytes`,
`duration`, `ordinal` and `slug` (of a 47-character title with accents).
Python's `humanize` and `python-slugify` are not installed on this machine,
so there is no Python column. Runs differed by under 3%. Measured
2026-11-25.

| Function | Calls/s |
|---|---|
| bytes | 678,196 |
| duration | 1,637,613 |
| ordinal | 4,048,457 |
| slug | 134,118 |
