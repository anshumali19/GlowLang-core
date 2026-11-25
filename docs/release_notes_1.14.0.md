# GlowLang 1.14.0

## New

- **Five more standard modules, written in GlowLang.** Each has a section in
  the [language reference](language_reference.md#18-standard-library), a
  test, and a benchmark in [benchmarks/libs](../benchmarks/libs/README.md).

  | Module | What it does |
  |---|---|
  | `cron` | Reads crontab lines (`*/15 9-17 * * mon-fri`, `@daily`) and says when one next runs |
  | `ini` | Reads and writes INI files, the format of Python's configparser |
  | `retry` | Calls a function again when it fails, waiting longer each time |
  | `textwrap` | `wrap`, `fill`, `shorten`, `indent` and `dedent`, as Python's textwrap |
  | `humanize` | `1.5 MB`, `1 hour 2 minutes`, `5 minutes ago`, `22nd`, `hello-world` |

  ```glow
  import "cron"
  import "datetime"
  import "humanize"

  let next = cron.next("0 9 * * mon-fri", datetime.now())
  show "next run", humanize.ago(next)        # in 13 hours, for example
  ```

- **Writing TOML and YAML:** `toml.stringify` / `toml.write` and
  `yaml.stringify` / `yaml.write`. What they write parses back to the same
  data; YAML quotes `yes`, `no`, `on` and `off` so that YAML 1.1 readers
  such as PyYAML read them as text.
- **YAML anchors, aliases and `<<` merge keys**, as CI and docker-compose
  files use them.
- **`schema` checks `format`:** `date`, `time`, `date-time`, `email`,
  `hostname`, `ipv4`, `ipv6`, `uri` and `uuid`. Before, every format passed.
- **`diff` finds a shortest diff for large changes too,** in memory that
  grows with the text, not the square of the changes: a 20,000-line text
  with one line in ten changed went from all lines removed and added
  (679 MB) to a 6,012-line diff (133 MB).
- **`glowlang new`** writes the settings of a new web API to `.env`, with
  a secret made for that project; `.gitignore` keeps the file out of git.
  The app, and a packaged app, read the `.env` next to themselves.
- `http.url_encode`, the counterpart of `http.url_decode`.

## Faster

Measured with [benchmarks/libs](../benchmarks/libs/README.md) on Linux
under WSL 2, on one machine. The 1.13.0 figures for `toml`, `csv` and `lru`
were taken the same day as the 1.14.0 ones; those for `url` and `semver`
the day before.

| | 1.13.0 | 1.14.0 | Python |
|---|---|---|---|
| `toml.parse` (parses/s) | 4,488 | 7,749 | 7,479 (`tomllib`) |
| `url.encode` (calls/s) | 267,388 | 8,274,786 | 1,446,273 (`quote`) |
| `csv` writing (rows/s) | 1,217,149 | 2,395,933 | 4,007,056 (C) |
| `csv` reading (rows/s) | 1,278,179 | 2,347,117 | 2,975,705 (C) |
| `semver` sort 1,000 (calls/s) | 228 | 558 | 578 (`packaging`) |
| `lru` lookups/s | 3,093,130 | 3,362,701 | 6,092,838 (C) |

`toml.parse` now splits the text into tokens with one regular expression
and reads those; on anything that reader does not cover it reads the text
character by character, as before, so error messages are unchanged.

The new modules against Python: `ini` parses 5.1× as fast as
`configparser`, and `textwrap` wraps 4.3× as fast as Python's `textwrap`.

The CPU benchmarks did not change, as nothing in the VM or JIT did: 1.4× to
7.9× faster than CPython ([benchmarks/RESULTS-linux.md](../benchmarks/RESULTS-linux.md)).
The loop row takes 8 or 9 ms from run to run, which moves its ratio between
6.7× (1.13.0's table) and 8×.

## Tests

- A packaged program is now checked on Linux, macOS and Windows to read
  the `.env` next to its executable.
- The REPL and bridge tests, which each failed once on CI and then passed,
  now report what they got when they fail.
