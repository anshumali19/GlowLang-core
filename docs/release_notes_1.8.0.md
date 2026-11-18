# GlowLang 1.8.0

## Changes that can need edits to your code

- `//` is now floor division, as in Python (`-7 // 2` is `-4`), and `//=`
  works too. Comments start with `#`, or use `/* ... */`. A line that starts
  with `//` was a comment before; change it to `#`.
- Calling a function, lambda, method or constructor with more arguments than
  it declares is an error. Fewer arguments are still allowed: the missing ones
  are `none`.
- Strings inside lists and dicts print with quotes (`["a", "b"]`), so
  different values no longer print the same way.
- A function that ends without `return` returns `none`.
- `min`, `max` and sorting raise an error on values that cannot be compared,
  instead of putting them in some order.
- `crypto.hmac` raises an error on bad arguments instead of returning `none`.
- Reading a function stored in an object's field gives the plain function:
  `h = o.handler; h(1, 2)` calls `handler(1, 2)`.

## New in the language

- `finally` blocks: `try / catch / finally` and `try / finally`. The block runs
  however the `try` is left, including by `return`, `stop`, `skip` or an
  uncaught error.
- Unpacking: `let a, b = 1, 2`, `a, b = b, a`, `let first, ...others = xs`,
  and `for each [a, b] in pairs`.
- Named arguments (`f(1, c = 9)`), `...rest` parameters, and `...spread` in
  calls and list literals.
- `??`, `??=` and `?.` for working with `none`.
- Sets (`{a, b}`, `set(items)`) with `| & - ^`, plus set and dict
  comprehensions.
- `match` / `case` with guards, list patterns and `otherwise`, and `enum`.
- Slice steps (`x[start:stop:step]`) and assignment to slices.
- Bitwise operators `& | ^ ~ << >>` on whole numbers of any size.
- Number literals `0xff`, `0b1010`, `0o17` and `1_000`.
- `super`, `class_name`, `is_a`, `enumerate`, `zip` and `to_string()`.

See [language_reference.md](language_reference.md).

## New libraries

- PostgreSQL client built into the runtime: TLS, SCRAM logins,
  parameterized queries, transactions. See [postgres.md](postgres.md).
- JWT and OAuth / OpenID Connect sign-ins, with RSA, ECDSA and HMAC signing
  in `crypto`. See [jwt_oauth.md](jwt_oauth.md).
- WebSocket client and server (RFC 6455), also in GlowWeb as
  `app.websocket(path, handler)`. See [websocket.md](websocket.md).
- Clients for Razorpay, WhatsApp, SMS through Twilio, and Google Sheets.
  See [services.md](services.md).

## Faster and leaner

- A generational garbage collector: most collections check only new objects.
- Stale references no longer keep finished data alive.
- Garbage-collection marking, `count`, `upper` / `lower` and calls to
  `...rest` functions are faster.

Compared with 1.7.0 (Linux x86-64, best of 7 runs):

| Benchmark | 1.7.0 | 1.8.0 |
|---|---|---|
| binary_trees | 0.513 s, 138 MB | 0.405 s, 76 MB |
| hashmap | 0.605 s, 128 MB | 0.508 s, 120 MB |
| strings | 0.222 s, 225 MB | 0.156 s, 215 MB |
| empty program | 18 MB | 11 MB |

The other benchmarks (fib, int_loop, mandelbrot, method_calls, nbody,
quicksort, sieve) run at the same speed and use 7 to 8 MB less memory.

## Fixed

- Scripts of about 1,750 statements or more no longer crash.
- `range` steps, `round(x, digits)`, f-string format specs, `trim(chars)`,
  dict keys that are numbers or booleans, catching a failed `assert`, and
  `type()` of a function give the right results.
- Integer `%` is exact past 64 bits.
- `spawn` gives missing parameters `none` instead of leftover values.
- GlowWeb handlers that take only `req` work again, and `req.get_header`
  ignores case.
- Editors (VS Code, Neovim, Helix, JetBrains, Sublime) highlight the new
  keywords and operators, and treat `#` as the comment.
