# GlowLang 1.10.0

## New

- **Decorators.** `@d` on the line above a `func` makes the name hold
  `d(func)`, as in Python. Several decorators apply bottom up, a decorator
  can take arguments (`@repeated(2)`), and they work on functions, nested
  functions, generator functions and methods. `functools.cache` remembers
  results, and GlowWeb's `route`, `get`, `post`, `put` and `del` work as
  decorators. See [Decorators](language_reference.md#decorators-).

  ```glow
  import "functools"

  @cache
  func fib(n):
      when n < 2:
          return n
      end
      return fib(n - 1) + fib(n - 2)
  end
  show fib(90)                     # 2880067194370816120, at once
  ```

  On a method, the decorator gets a plain function whose first argument is
  the object, and every call of the method (from outside, through `self`
  or through `super`) goes through what it returns.

- **`**opts` and `**dict`.** A last parameter written `**opts` collects the
  `name = value` arguments no other parameter takes, as a dict. In a call,
  `**d` passes a dict's entries as `name = value` arguments. Together with
  `...rest` and `...spread`, a wrapper can pass on every argument it gets:

  ```glow
  func tag(name, ...children, **attrs):
      return name + " " + str(children) + " " + str(attrs)
  end
  show tag("p", "hi", id = "intro")   # p ["hi"] {"id": "intro"}

  func logged(f):
      return lambda(...args, **opts):
          show "calling " + str(args) + " " + str(opts)
          return f(...args, **opts)
      end
  end
  ```

- **Multi-line input in the REPL.** A block (`func`, `when`, `for`,
  `class`, ...) continues on `....` lines until its `end`, and so does a
  line that leaves a bracket open. An expression on its own shows its value,
  and `glowlang repl < file.glow` runs a file as if it were typed in.

## Faster

- A call with `name = value` arguments to a function defined in the program
  is matched to the parameters when the program is compiled, and runs as
  fast as a plain call (Linux, compiled, per call):

  | Call | 1.9.0 | 1.10.0 |
  |---|---:|---:|
  | `f(i, c = 9)` | 65 ns | 21 ns |
  | `f(i, 0, 9)` (plain, for comparison) | 22 ns | 23 ns |

  Calls to methods, constructors and lambdas are matched when they run, as
  before.

The benchmark suite runs as fast as 1.9.0.

## Fixed

- `f = wrap(f)` did nothing for calls to `f`: calls by name found the
  function before the variable. Assigning to a function's name now changes
  what every call by that name runs.

## Good to know

- `@`, `**` in a parameter list and `**` before an argument are new syntax;
  programs that ran on 1.9.0 parse the same.
- A decorator on a method of a `trait` is an error (reported when the trait
  is made): decorate the method in the class instead.
