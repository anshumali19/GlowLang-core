# GlowLang 1.9.0

## New

- **Generators.** A function, method or lambda whose body contains `yield`
  returns a generator. Its body runs a piece at a time as you step it with
  `for each`, a comprehension, `next(g)` or `next(g, default)`, so values are
  made only when needed and a generator can be endless. `yield from` hands
  over every item of a list, set, string or another generator. See
  [Generators](language_reference.md#generators-yield).

  ```glow
  func count_up(limit):
      let i = 0
      while i < limit:
          yield i
          i += 1
      end
  end
  show [n * n for each n in count_up(4)]   # [0, 1, 4, 9]
  ```

- **`with` blocks.** `with x as name:` ... `end` uses a resource for a block
  and lets go of it however the block is left, including on an error,
  `return`, `stop` or `skip`. An object takes part with `enter()` and
  `exit(error)`, or just `close()`: Postgres connections, WebSockets, SQLite
  handles, `Mutex` (locked for the block) and the ORM's `Transaction`
  (committed, or rolled back when the block raises) all work. See
  [With](language_reference.md#with).

  ```glow
  with sqlite.open("shop.db") as db:
      sqlite.exec(db, "insert into orders (item) values ('pen')")
  end                     # closed here, error or not
  ```

- **Compound assignment on fields and items:** `self.count += 1`,
  `counts[word] += 1`, `grid[r][c] -= 1`, for every compound operator. The
  object and the index are worked out once, as in Python.
- **Multi-line lambdas inside brackets:** `xs.map(lambda(x): ... end)`,
  `{"f": lambda(x): ... end}` and callbacks passed to functions, which were
  syntax errors.

## Faster

- Calls with `name = value` or `...spread` arguments make no lists of their
  own any more (Linux, compiled, per call):

  | Call | 1.8.0 | 1.9.0 |
  |---|---:|---:|
  | `f(i, c = 9)` | 100 ns | 69 ns |
  | `f(...xs)` | 103 ns | 59 ns |
  | `o.m(i, c = 9)` | 117 ns | 87 ns |
  | `P(i, y = 2)` | 108 ns | 83 ns |

- A million `yield`s take 38 ms (CPython 3.14: 44 to 51 ms).

The benchmark suite otherwise runs as fast as 1.8.0. `method_calls` measures
up to 6% apart between builds, and moves back and forth across changes that
do not touch the code it runs, so that difference is code layout, not this
release. Against CPython 3.14 GlowLang is 1.4× to 7.9× faster on the README's
benchmarks.

## Fixed

- Calls made from native code dropped every argument after the 16th: a
  spread or `name = value` call, or a callback from `map` or `sorted`, gave
  those parameters their defaults.
- An error that passed through a `finally` block was reported at the last
  line of the `finally`, not where it was raised.
- `x.name op= v` and `x[i] op= v` were syntax errors, reported at the line
  after the statement.
- The REPL said "GlowLang v1.0"; it shows the real version.

## Good to know

- `yield`, `with` and `as` are keywords only where they are used (`yield`
  and `with` at the start of a statement, `as` in a `with` header), so
  variables with those names keep working.
- Editors (VS Code, Sublime, Neovim, JetBrains) highlight the new keywords,
  and the language server completes them.
