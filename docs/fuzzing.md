# Fuzzing and usage numbers

## Differential fuzzer: `tools/fuzz/difffuzz.py`

The C harnesses (`make fuzz-all`) look for crashes. Most of GlowLang's
serious bugs, though, gave a wrong answer without crashing: a stale constant
in the optimizer, leftover values in missing arguments, a tail call that
corrupted the stack. `difffuzz.py` is built to catch that kind of bug.

Each round it writes one random program twice, in GlowLang and in Python,
with the same meaning, and runs it three ways:

| Run | Command |
|---|---|
| reference | `python3` |
| VM only | `glowlang` with `GLOW_NOJITEXEC=1` |
| VM and JIT | `glowlang` |

All three outputs must match. The programs use integers (kept small by a
helper `m()`), lists, `if`/`eif`/`else`, `while` with `skip` and `stop`,
`every`, `for each`, functions (some recursive, some ending in a tail call),
a class with methods, and `try`/`throw`. The main loop runs up to 1,200
times, which is enough to make the JIT compile both the functions and the
loop.

```bash
make
python3 tools/fuzz/difffuzz.py --glow bin/glowlang --rounds 500
```

| Option | Meaning |
|---|---|
| `--rounds N` | number of programs (default 200) |
| `--seed S` | first seed; program *i* uses seed S + *i*, so any run can be repeated |
| `--out DIR` | where failures are saved (default `fuzz-out/`) |
| `--keep` | keep every program, not only the failing ones |

A failure is saved as `SEED.glow`, `SEED.py` and `SEED.txt`, where the
`.txt` file says which run crashed, hung or printed something different. To
replay one: `--seed SEED --rounds 1 --keep`.

The script exits with 1 if any program failed. A round is counted as
*skipped* when the Python version itself fails or takes over 20 seconds. That
means the generator went wrong, not GlowLang.

Before adding a new kind of statement to the generator, make sure it means
the same in both languages. One example: assigning to the variable of an
`every` loop changes how many times the loop runs, while Python's `for`
ignores such an assignment. So the generator never assigns to a loop
variable.

## Usage numbers: `tools/stats.sh`

```bash
tools/stats.sh              # print today's numbers
tools/stats.sh stats.csv    # also add a row to stats.csv
```

It uses the GitHub CLI (`gh`, signed in with push access) to report:

- stars, forks and watchers
- unique cloners and visitors over the last 14 days
- release downloads
- issues and pull requests opened by other people
- public `.glow` files that use `eif`, written by people other than the owner

A number it cannot fetch, for example because of a rate limit, prints as
`?`. Microsoft Store installs are only shown in Partner Center (Analytics,
then Acquisitions); copy that number into the `store_installs` column by hand.
