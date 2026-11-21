# GlowLang 1.11.0

## New

- **`parallel.map`: use every core.** GlowLang code runs on one core at a
  time, even with several threads. `parallel.map` runs a function over a
  list in GlowLang worker processes, one per core by default, and returns
  the results in order. It works the same on Windows, Linux and macOS. See
  [Using every core](process_guide.md#using-every-core-import-parallel).

  ```glow
  import "parallel"
  let sizes = parallel.map("work.glow", "count_words", files)
  let sizes = parallel.map("work.glow", "count_words", files, workers = 4)
  ```

  The function lives in a module of its own (`work.glow` above). Items and
  results travel as JSON, and the first error the function raises is raised
  by `parallel.map`, naming the item. 24 calls of a function taking 43 ms
  ran 7.8× faster with 24 workers. `os.cpu_count()` is new too.

- **Closing generators.** `g.close()` finishes a generator early: the
  `finally` blocks around the `yield` it is paused at run, and `next(g)`
  then finds it finished. `with g as x:` closes it when the block ends. A
  generator dropped while paused inside a `try` still runs its `finally`
  blocks, once nothing uses it any more or at the latest when the program
  ends.

- **`for each i, x in g`** over a generator numbers its values (0, 1, 2,
  ...), as for a list, so it works on an endless generator too.

- **Decorators on trait methods.** The decorators run once, when the trait
  is made, and every class that uses the trait gets the decorated method.

## Faster

- `name = value` arguments to **methods and constructors** are now matched
  to the parameters when the program is compiled, as they were for
  functions in 1.10.0 (Linux, compiled, per call):

  | Call | 1.10.0 | 1.11.0 |
  |---|---:|---:|
  | `o.m(i, c = 9)` | 90 ns | 28 ns |
  | `P(i, y = 2)` | 88 ns | 46 ns |

  A call is still matched when it runs if the method name belongs to more
  than one class, to a field or dict key, or to a built-in method such as
  `sort`, and for a class whose `init` comes from a trait.

- Functions that use `show` or `throw` are compiled now; before, any such
  function ran entirely in the interpreter. A call to one is about 4×
  faster (`throw`: 36 → 8.5 ns, `show`: 29 → 6.5 ns).

The benchmark suite runs as fast as 1.10.0.

## Fixed

- A trait defined after a class gave its methods to that class, leaving the
  trait empty.

## Good to know

- `for each i, x in g` over a generator used to be an error; programs that
  ran on 1.10.0 run the same.
- A `catch` inside a generator does not see `close()`, as Python's
  `except Exception` does not see `GeneratorExit`.
